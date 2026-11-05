// tinywfc - wave function collapse in a single C++23 header
// https://github.com/varnholt/tinywfc, MIT license
//
// the simple tiled model on 2d and 3d grids:
//
//    tinywfc::Rules rules{3};                                       // three tiles
//    rules.allow(0, tinywfc::Direction::PositiveX, 1);              // tile 1 may sit right of tile 0 (and 0 left of 1)
//    ...
//    auto solver = tinywfc::Solver::create(rules, {._size = {16, 1, 16}, ._seed = 42});
//    if (solver && solver->run())
//    {
//       const auto tiles = solver->result();                        // one tile id per cell
//    }
//
// a cell starts with every tile possible. each step observes the cell with the lowest entropy, picks one of its tiles
// by weight and propagates the adjacency rules to its neighbors (ac-4: support counts per tile and direction, so a
// removal only touches the tiles it supported). a contradiction (a cell without any possible tile) is undone by
// backtracking along a trail of removals; when that runs out, run() starts the next attempt with another seed, on
// several threads if Settings::_threads asks for it.
//
// results are reproducible: the same rules, settings and seed give the same grid, with any thread count. the random
// numbers come from xoshiro256**, not from the standard distributions, which differ between standard libraries.
// the header needs nothing but the standard library.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace tinywfc
{

inline constexpr int32_t version_major = 1;
inline constexpr int32_t version_minor = 0;
inline constexpr int32_t version_patch = 0;

using TileId = uint32_t;

// ----------------------------------------------------------------------------------------------------
// grid geometry
// ----------------------------------------------------------------------------------------------------

/// \brief the six neighbors of a cell. y is up; 2d grids simply use a size of 1 along y (or z).
enum class Direction : uint8_t
{
   PositiveX = 0,
   NegativeX = 1,
   PositiveY = 2,
   NegativeY = 3,
   PositiveZ = 4,
   NegativeZ = 5,
};

inline constexpr std::array<Direction, 6> directions{
   Direction::PositiveX,
   Direction::NegativeX,
   Direction::PositiveY,
   Direction::NegativeY,
   Direction::PositiveZ,
   Direction::NegativeZ,
};

[[nodiscard]] constexpr std::size_t index(Direction direction) noexcept
{
   return static_cast<std::size_t>(direction);
}

[[nodiscard]] constexpr Direction opposite(Direction direction) noexcept
{
   // the directions come in pairs, so flipping the lowest bit swaps positive and negative
   return static_cast<Direction>(static_cast<uint8_t>(direction) ^ 1u);
}

struct Size
{
   int32_t x{1};
   int32_t y{1};
   int32_t z{1};

   [[nodiscard]] constexpr int64_t cellCount() const noexcept
   {
      return static_cast<int64_t>(x) * y * z;
   }

   friend constexpr bool operator==(const Size&, const Size&) = default;
};

struct Coordinate
{
   int32_t x{0};
   int32_t y{0};
   int32_t z{0};

   friend constexpr bool operator==(const Coordinate&, const Coordinate&) = default;
};

/// \brief the neighboring coordinate in a direction; may lie outside the grid.
[[nodiscard]] constexpr Coordinate adjacent(const Coordinate& coordinate, Direction direction) noexcept
{
   switch (direction)
   {
      case Direction::PositiveX:
         return {coordinate.x + 1, coordinate.y, coordinate.z};
      case Direction::NegativeX:
         return {coordinate.x - 1, coordinate.y, coordinate.z};
      case Direction::PositiveY:
         return {coordinate.x, coordinate.y + 1, coordinate.z};
      case Direction::NegativeY:
         return {coordinate.x, coordinate.y - 1, coordinate.z};
      case Direction::PositiveZ:
         return {coordinate.x, coordinate.y, coordinate.z + 1};
      case Direction::NegativeZ:
         return {coordinate.x, coordinate.y, coordinate.z - 1};
   }
   return coordinate;
}

// ----------------------------------------------------------------------------------------------------
// rules
// ----------------------------------------------------------------------------------------------------

/// \brief which tile may sit next to which, and how often each tile should be picked.
class Rules
{
public:
   /// \param tile_count number of tiles; tile ids are 0 .. tile_count - 1, all with weight 1
   explicit Rules(std::size_t tile_count = 0)
       : _tile_count(tile_count), _weights(tile_count, 1.0f), _allowed(tile_count * tile_count * 6, false)
   {
   }

   [[nodiscard]] std::size_t tileCount() const noexcept
   {
      return _tile_count;
   }

   /// \brief relative frequency; higher weights are picked more often. must be greater than 0.
   void setWeight(TileId tile, float weight)
   {
      _weights.at(tile) = weight;
   }

   [[nodiscard]] float weight(TileId tile) const
   {
      return _weights.at(tile);
   }

   [[nodiscard]] std::span<const float> weights() const noexcept
   {
      return _weights;
   }

   /// \brief lets `neighbor` sit on the `direction` side of `tile`, and `tile` on the opposite side of `neighbor`.
   void allow(TileId tile, Direction direction, TileId neighbor)
   {
      _allowed.at(slot(tile, direction, neighbor)) = true;
      _allowed.at(slot(neighbor, opposite(direction), tile)) = true;
   }

   /// \brief allows a pair on all six sides, for tiles that may touch in any direction.
   void allowEverywhere(TileId tile, TileId neighbor)
   {
      for (const auto direction : directions)
      {
         allow(tile, direction, neighbor);
      }
   }

   [[nodiscard]] bool allows(TileId tile, Direction direction, TileId neighbor) const
   {
      return _allowed.at(slot(tile, direction, neighbor));
   }

private:
   [[nodiscard]] std::size_t slot(TileId tile, Direction direction, TileId neighbor) const noexcept
   {
      return (static_cast<std::size_t>(tile) * 6 + index(direction)) * _tile_count + neighbor;
   }

   std::size_t _tile_count{0};
   std::vector<float> _weights;
   std::vector<bool> _allowed;
};

/// \brief a tile described by a socket on each of its six sides, indexed by Direction. two tiles fit next to each other
///        when the touching sockets are equal. sockets can be any equality comparable type: ints, enums, string_views.
template <class Socket>
struct SocketTile
{
   std::array<Socket, 6> _sockets{};
   float _weight{1.0f};
};

/// \brief builds rules from sockets: `b` may sit on side d of `a` when a's socket d equals b's socket on the opposite side.
template <class Socket>
[[nodiscard]] Rules rulesFromSockets(std::span<const SocketTile<Socket>> tiles)
{
   Rules rules{tiles.size()};
   for (std::size_t a = 0; a < tiles.size(); ++a)
   {
      rules.setWeight(static_cast<TileId>(a), tiles[a]._weight);
      for (std::size_t b = 0; b < tiles.size(); ++b)
      {
         for (const auto direction : directions)
         {
            if (tiles[a]._sockets[index(direction)] == tiles[b]._sockets[index(opposite(direction))])
            {
               rules.allow(static_cast<TileId>(a), direction, static_cast<TileId>(b));
            }
         }
      }
   }
   return rules;
}

/// \brief the sockets of a tile turned by quarter_turns * 90 degrees around +y, counter clockwise seen from above
///        (the right handed rotation): the side that faced +x then faces -z. y sockets stay where they are.
template <class Socket>
[[nodiscard]] constexpr std::array<Socket, 6> rotateY(std::array<Socket, 6> sockets, int32_t quarter_turns)
{
   const auto turns = ((quarter_turns % 4) + 4) % 4;
   for (auto turn = 0; turn < turns; ++turn)
   {
      const auto rotated = sockets;
      sockets[index(Direction::NegativeZ)] = rotated[index(Direction::PositiveX)];
      sockets[index(Direction::NegativeX)] = rotated[index(Direction::NegativeZ)];
      sockets[index(Direction::PositiveZ)] = rotated[index(Direction::NegativeX)];
      sockets[index(Direction::PositiveX)] = rotated[index(Direction::PositiveZ)];
   }
   return sockets;
}

// ----------------------------------------------------------------------------------------------------
// solver
// ----------------------------------------------------------------------------------------------------

enum class Error : int32_t
{
   InvalidSize,        //!< every axis must be at least 1, and the grid must fit into memory
   InvalidRules,       //!< no tiles, more than 65534 tiles, or a weight that is not a positive finite number
   InvalidCoordinate,  //!< outside the grid
   InvalidTile,        //!< tile id >= tile count
   Contradiction,      //!< the constraints leave a cell without any possible tile, even after backtracking and restarts
};

[[nodiscard]] constexpr std::string_view describe(Error error) noexcept
{
   switch (error)
   {
      case Error::InvalidSize:
         return "grid size must be at least 1 on every axis";
      case Error::InvalidRules:
         return "rules need 1 to 65534 tiles and positive weights";
      case Error::InvalidCoordinate:
         return "coordinate is outside the grid";
      case Error::InvalidTile:
         return "tile id is out of range";
      case Error::Contradiction:
         return "no tiling satisfies the rules";
   }
   return "unknown error";
}

struct Settings
{
   Size _size{8, 1, 8};
   uint64_t _seed{0};
   bool _periodic{false};            //!< opposite borders are neighbors on every axis longer than 1, so the result tiles
   int32_t _max_backtracks{10'000};  //!< per attempt; 0 turns backtracking off
   int32_t _max_restarts{10};        //!< further attempts with a new seed after backtracking gave up
   int32_t _threads{1};              //!< attempts run() tries at the same time; 0 = one per hardware thread; always 1 without threads
};

struct Statistics
{
   int32_t _decisions{0};   //!< observed cells in the attempt that produced the result
   int32_t _backtracks{0};  //!< decisions undone in that attempt
   int32_t _restarts{0};    //!< index of that attempt: 0 = the first one succeeded
};

/// \brief solves one grid.
///
/// a Solver is a value: copy it to try something without touching the original. it is not thread safe, use one solver
/// from one thread at a time. Rules are only read, so one Rules object may be used by any number of threads at once.
/// run() with Settings::_threads > 1 works on private copies internally and calls no callbacks from other threads.
class Solver
{
public:
   using CollapseCallback = std::function<void(const Coordinate&, TileId)>;
   using ResetCallback = std::function<void()>;

   /// \brief a solver with every tile possible in every cell.
   [[nodiscard]] static std::expected<Solver, Error> create(const Rules& rules, const Settings& settings)
   {
      const auto& size = settings._size;
      if (size.x < 1 || size.y < 1 || size.z < 1 || size.cellCount() >= std::numeric_limits<int32_t>::max())
      {
         return std::unexpected{Error::InvalidSize};
      }
      if (rules.tileCount() == 0 || rules.tileCount() >= no_support ||
          std::ranges::any_of(rules.weights(), [](float weight) { return !(weight > 0.0f) || !std::isfinite(weight); }))
      {
         return std::unexpected{Error::InvalidRules};
      }
      return Solver{rules, settings};
   }

   [[nodiscard]] const Size& size() const noexcept
   {
      return _settings._size;
   }

   [[nodiscard]] const Settings& settings() const noexcept
   {
      return _settings;
   }

   [[nodiscard]] std::size_t tileCount() const noexcept
   {
      return _tables->_tile_count;
   }

   [[nodiscard]] const Statistics& statistics() const noexcept
   {
      return _statistics;
   }

   /// \brief called whenever a cell is down to one tile, by a decision or by propagation.
   void setCollapseCallback(CollapseCallback callback)
   {
      _collapse_callback = std::move(callback);
   }

   /// \brief called after a backtrack or a restart; cells reported as collapsed before may be open again.
   void setResetCallback(ResetCallback callback)
   {
      _reset_callback = std::move(callback);
   }

   // ------------------------------------------------------------------------------------------------
   // constraints
   // ------------------------------------------------------------------------------------------------

   /// \brief limits a cell to the given tiles. constraints set before the first step survive restarts; later ones can
   ///        be undone by backtracking.
   std::expected<void, Error> restrict(const Coordinate& coordinate, std::span<const TileId> allowed)
   {
      if (!contains(coordinate))
      {
         return std::unexpected{Error::InvalidCoordinate};
      }
      if (std::ranges::any_of(allowed, [&](TileId tile) { return tile >= tileCount(); }))
      {
         return std::unexpected{Error::InvalidTile};
      }
      const auto cell = cellIndex(coordinate);
      for (TileId tile = 0; tile < tileCount(); ++tile)
      {
         if (has(cell, tile) && std::ranges::find(allowed, tile) == allowed.end() && !remove(cell, tile))
         {
            _pending.clear();
            return std::unexpected{Error::Contradiction};
         }
      }
      return propagate() ? std::expected<void, Error>{} : std::unexpected{Error::Contradiction};
   }

   /// \brief rules out one tile in a cell.
   std::expected<void, Error> ban(const Coordinate& coordinate, TileId tile)
   {
      if (!contains(coordinate))
      {
         return std::unexpected{Error::InvalidCoordinate};
      }
      if (tile >= tileCount())
      {
         return std::unexpected{Error::InvalidTile};
      }
      if (!remove(cellIndex(coordinate), tile) || !propagate())
      {
         _pending.clear();
         return std::unexpected{Error::Contradiction};
      }
      return {};
   }

   // ------------------------------------------------------------------------------------------------
   // solving
   // ------------------------------------------------------------------------------------------------

   /// \brief observes one cell and propagates. a contradiction is backtracked within the step.
   /// \return true while cells are left, false when the grid is complete, Error::Contradiction when backtracking gave up
   std::expected<bool, Error> step()
   {
      if (!start())
      {
         return std::unexpected{Error::Contradiction};
      }
      if (finished())
      {
         return false;
      }

      const auto cell = pickCell();
      const auto tile = pickTile(cell);
      if (_settings._max_backtracks > 0)
      {
         _decisions.push_back({static_cast<uint32_t>(cell), tile, _trail.size()});
      }
      ++_statistics._decisions;

      auto consistent = collapse(cell, tile);
      if (consistent && _decisions.size() > _backjump_depth)
      {
         // past the place of the last contradiction: that one is solved
         _backjump_streak = 0;
      }
      while (!consistent)
      {
         if (_decisions.empty() || _statistics._backtracks >= _settings._max_backtracks)
         {
            return std::unexpected{Error::Contradiction};
         }
         // backjumping: contradictions in a row undo 1, 2, 4, ... decisions, so a cause far back is reached quickly
         // instead of trying every combination of the latest decisions. the earliest undone choice is banned.
         _backjump_depth = _decisions.size();
         const auto jump = std::min(_decisions.size(), std::size_t{1} << std::min(_backjump_streak, 16));
         ++_backjump_streak;
         const auto decision = _decisions[_decisions.size() - jump];
         _decisions.resize(_decisions.size() - jump);
         undo(decision._trail_size);
         ++_statistics._backtracks;
         if (_reset_callback)
         {
            _reset_callback();
         }
         consistent = remove(decision._cell, decision._tile) && propagate();
         if (!consistent)
         {
            _pending.clear();
         }
      }
      return !finished();
   }

   /// \brief steps until the grid is complete. when backtracking gives up, the next attempt starts over from the
   ///        constraints set before the first step, with another seed. with Settings::_threads > 1, attempts run in
   ///        parallel; the result is the same as with one thread: the lowest numbered attempt that succeeds.
   std::expected<void, Error> run()
   {
      if (!start())
      {
         return std::unexpected{Error::Contradiction};
      }
      const auto threads = threadCount();
      if (threads > 1 && _settings._max_restarts > 0 && _statistics._decisions == 0 && _attempt == 0)
      {
         // most grids succeed in the first attempt; copying the state for other threads only pays when it fails
         if (runAttempt({}))
         {
            return {};
         }
         return runParallel(threads, 1);
      }

      while (true)
      {
         if (runAttempt({}))
         {
            return {};
         }
         if (_attempt >= _settings._max_restarts)
         {
            return std::unexpected{Error::Contradiction};
         }
         restart();
      }
   }

   /// \brief back to the constraints set before the first step, as the next attempt with its own seed.
   void restart()
   {
      if (!start())
      {
         return;
      }
      beginAttempt(_attempt + 1);
      if (_reset_callback)
      {
         _reset_callback();
      }
   }

   [[nodiscard]] bool finished() const noexcept
   {
      return _state._collapsed_count == _tables->_cell_count;
   }

   /// \brief cells down to one tile; cellCount() when finished.
   [[nodiscard]] std::size_t collapsedCount() const noexcept
   {
      return _state._collapsed_count;
   }

   [[nodiscard]] std::size_t cellCount() const noexcept
   {
      return _tables->_cell_count;
   }

   // ------------------------------------------------------------------------------------------------
   // state
   // ------------------------------------------------------------------------------------------------

   [[nodiscard]] bool contains(const Coordinate& coordinate) const noexcept
   {
      const auto& size = _settings._size;
      return coordinate.x >= 0 && coordinate.y >= 0 && coordinate.z >= 0 && coordinate.x < size.x && coordinate.y < size.y &&
             coordinate.z < size.z;
   }

   /// \brief cells are stored x fastest, then z, then y, so every horizontal layer is contiguous.
   [[nodiscard]] std::size_t cellIndex(const Coordinate& coordinate) const noexcept
   {
      const auto& size = _settings._size;
      return (static_cast<std::size_t>(coordinate.y) * static_cast<std::size_t>(size.z) + static_cast<std::size_t>(coordinate.z)) *
                static_cast<std::size_t>(size.x) +
             static_cast<std::size_t>(coordinate.x);
   }

   [[nodiscard]] Coordinate coordinate(std::size_t cell) const noexcept
   {
      const auto& size = _settings._size;
      const auto layer = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.z);
      return {
         static_cast<int32_t>(cell % static_cast<std::size_t>(size.x)),
         static_cast<int32_t>(cell / layer),
         static_cast<int32_t>((cell % layer) / static_cast<std::size_t>(size.x)),
      };
   }

   /// \return the tile of a collapsed cell
   [[nodiscard]] std::optional<TileId> tile(const Coordinate& coordinate) const
   {
      if (!contains(coordinate))
      {
         return std::nullopt;
      }
      const auto cell = cellIndex(coordinate);
      if (_state._counts[cell] != 1)
      {
         return std::nullopt;
      }
      return firstTile(cell);
   }

   /// \brief how many tiles are still possible in a cell; 1 = collapsed.
   [[nodiscard]] int32_t possibilityCount(const Coordinate& coordinate) const
   {
      return contains(coordinate) ? _state._counts[cellIndex(coordinate)] : 0;
   }

   [[nodiscard]] bool isPossible(const Coordinate& coordinate, TileId tile) const
   {
      return contains(coordinate) && tile < tileCount() && has(cellIndex(coordinate), tile);
   }

   /// \return one tile per cell in cellIndex() order, or an empty vector while the grid is not finished
   [[nodiscard]] std::vector<TileId> result() const
   {
      if (!finished())
      {
         return {};
      }
      std::vector<TileId> tiles(cellCount());
      for (std::size_t cell = 0; cell < cellCount(); ++cell)
      {
         tiles[cell] = firstTile(cell);
      }
      return tiles;
   }

private:
   static constexpr uint32_t no_neighbor = std::numeric_limits<uint32_t>::max();
   static constexpr uint16_t no_support = std::numeric_limits<uint16_t>::max();  //!< support count on a grid border

   /// \brief xoshiro256**, seeded with splitmix64.
   class Random
   {
   public:
      explicit Random(uint64_t seed = 0)
      {
         for (auto& word : _state)
         {
            seed += 0x9e3779b97f4a7c15ull;
            auto mixed = seed;
            mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ull;
            mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebull;
            word = mixed ^ (mixed >> 31);
         }
      }

      uint64_t next() noexcept
      {
         const auto result = std::rotl(_state[1] * 5, 7) * 9;
         const auto shifted = _state[1] << 17;
         _state[2] ^= _state[0];
         _state[3] ^= _state[1];
         _state[1] ^= _state[2];
         _state[0] ^= _state[3];
         _state[2] ^= shifted;
         _state[3] = std::rotl(_state[3], 45);
         return result;
      }

      /// \return a number in [0, 1)
      double unit() noexcept
      {
         return static_cast<double>(next() >> 11) * 0x1.0p-53;
      }

   private:
      std::array<uint64_t, 4> _state{};
   };

   /// \brief everything derived from the rules and the grid shape; shared between copies, never changed after create().
   struct Tables
   {
      std::size_t _tile_count{0};
      std::size_t _words{0};
      std::size_t _cell_count{0};
      std::vector<double> _weights;
      std::vector<double> _weight_log_weights;
      std::vector<uint32_t> _neighbors;  //!< [cell * 6 + direction], no_neighbor at a border

      // tiles with the same list of compatible tiles on a side form a class; supports are counted per class, so
      // socket based rules with many tiles per socket need one counter where plain ac-4 needs dozens
      std::size_t _class_count{0};
      std::vector<uint8_t> _class_direction;       //!< [class]: the side the class describes
      std::vector<uint16_t> _class_size;           //!< [class]: number of compatible tiles, the initial support
      std::vector<uint32_t> _class_members_start;  //!< [class] .. [class + 1] index _class_members
      std::vector<TileId> _class_members;          //!< tiles of the class
      std::vector<uint32_t> _affected_start;       //!< [tile * 6 + direction] .. [+ 1] index _affected_classes
      std::vector<uint32_t> _affected_classes;     //!< classes on the opposite side whose list contains the tile
   };

   /// \brief what an attempt changes. the support counts make propagation ac-4: a tile leaves a cell as soon as one
   ///        side has no compatible tile left in that neighbor, without rescanning the neighbor's domain.
   struct State
   {
      std::vector<uint64_t> _domains;        //!< [cell * words + word], one bit per tile
      std::vector<int32_t> _counts;          //!< possible tiles per cell
      std::vector<double> _weight_sums;      //!< sum of weights of the possible tiles
      std::vector<double> _weight_log_sums;  //!< sum of w log w of the possible tiles
      std::vector<uint16_t> _supports;       //!< [cell * classes + class]: compatible tiles left in that neighbor
      std::size_t _collapsed_count{0};
   };

   struct Removal
   {
      uint32_t _cell{0};
      TileId _tile{0};
   };

   struct Decision
   {
      uint32_t _cell{0};
      TileId _tile{0};
      std::size_t _trail_size{0};
   };

   struct HeapEntry
   {
      double _key{0.0};
      uint32_t _cell{0};

      friend bool operator<(const HeapEntry& lhs, const HeapEntry& rhs) noexcept
      {
         // std::push_heap builds a max heap, the lowest entropy has to come first
         return lhs._key > rhs._key || (lhs._key == rhs._key && lhs._cell > rhs._cell);
      }
   };

   Solver(const Rules& rules, const Settings& settings) : _settings(settings), _random(settings._seed)
   {
      auto tables = std::make_shared<Tables>();
      const auto tile_count = rules.tileCount();
      tables->_tile_count = tile_count;
      tables->_words = (tile_count + 63) / 64;
      tables->_cell_count = static_cast<std::size_t>(settings._size.cellCount());
      tables->_weights.assign(rules.weights().begin(), rules.weights().end());
      tables->_weight_log_weights.resize(tile_count);
      std::ranges::transform(
         tables->_weights, tables->_weight_log_weights.begin(), [](double weight) { return weight * std::log(weight); }
      );

      // classes: per side, every distinct list of compatible tiles
      std::vector<std::vector<TileId>> class_lists;
      std::vector<std::vector<TileId>> class_members;
      for (const auto direction : directions)
      {
         std::map<std::vector<TileId>, uint32_t> classes_of_side;
         for (TileId tile = 0; tile < tile_count; ++tile)
         {
            std::vector<TileId> compatible;
            for (TileId neighbor = 0; neighbor < tile_count; ++neighbor)
            {
               if (rules.allows(tile, direction, neighbor))
               {
                  compatible.push_back(neighbor);
               }
            }
            const auto [entry, added] = classes_of_side.try_emplace(compatible, static_cast<uint32_t>(class_lists.size()));
            if (added)
            {
               class_lists.push_back(compatible);
               class_members.emplace_back();
               tables->_class_direction.push_back(static_cast<uint8_t>(index(direction)));
               tables->_class_size.push_back(static_cast<uint16_t>(compatible.size()));
            }
            class_members[entry->second].push_back(tile);
         }
      }
      tables->_class_count = class_lists.size();
      for (const auto& members : class_members)
      {
         tables->_class_members_start.push_back(static_cast<uint32_t>(tables->_class_members.size()));
         tables->_class_members.insert(tables->_class_members.end(), members.begin(), members.end());
      }
      tables->_class_members_start.push_back(static_cast<uint32_t>(tables->_class_members.size()));

      // removing tile w from a cell lowers, in the neighbor on side d, the classes of side opposite(d) that list w
      std::vector<std::vector<uint32_t>> affected(tile_count * 6);
      for (std::size_t class_index = 0; class_index < class_lists.size(); ++class_index)
      {
         const auto towards_cell = tables->_class_direction[class_index] ^ 1u;
         for (const auto tile : class_lists[class_index])
         {
            affected[tile * 6 + towards_cell].push_back(static_cast<uint32_t>(class_index));
         }
      }
      for (const auto& classes : affected)
      {
         tables->_affected_start.push_back(static_cast<uint32_t>(tables->_affected_classes.size()));
         tables->_affected_classes.insert(tables->_affected_classes.end(), classes.begin(), classes.end());
      }
      tables->_affected_start.push_back(static_cast<uint32_t>(tables->_affected_classes.size()));

      // neighbors once, so propagation does no coordinate math
      tables->_neighbors.resize(tables->_cell_count * 6);
      for (std::size_t cell = 0; cell < tables->_cell_count; ++cell)
      {
         for (const auto direction : directions)
         {
            const auto next = neighborOf(cell, direction);
            tables->_neighbors[cell * 6 + index(direction)] = next ? static_cast<uint32_t>(*next) : no_neighbor;
         }
      }
      _tables = std::move(tables);

      // every cell starts with every tile; the support of a tile in a direction is the number of compatible tiles
      const auto& t = *_tables;
      std::vector<uint64_t> full(t._words, ~uint64_t{0});
      if (tile_count % 64 != 0)
      {
         full.back() = (uint64_t{1} << (tile_count % 64)) - 1;
      }
      auto& state = _state;
      state._domains.reserve(t._cell_count * t._words);
      for (std::size_t cell = 0; cell < t._cell_count; ++cell)
      {
         state._domains.insert(state._domains.end(), full.begin(), full.end());
      }
      state._counts.assign(t._cell_count, static_cast<int32_t>(tile_count));
      const auto weight_sum = std::accumulate(t._weights.begin(), t._weights.end(), 0.0);
      const auto weight_log_sum = std::accumulate(t._weight_log_weights.begin(), t._weight_log_weights.end(), 0.0);
      state._weight_sums.assign(t._cell_count, weight_sum);
      state._weight_log_sums.assign(t._cell_count, weight_log_sum);
      state._supports.resize(t._cell_count * t._class_count);
      for (std::size_t cell = 0; cell < t._cell_count; ++cell)
      {
         for (std::size_t class_index = 0; class_index < t._class_count; ++class_index)
         {
            const auto has_neighbor = t._neighbors[cell * 6 + t._class_direction[class_index]] != no_neighbor;
            state._supports[cell * t._class_count + class_index] = has_neighbor ? t._class_size[class_index] : no_support;
         }
      }
      state._collapsed_count = tile_count == 1 ? t._cell_count : 0;
   }

   [[nodiscard]] std::optional<std::size_t> neighborOf(std::size_t cell, Direction direction) const noexcept
   {
      auto next = adjacent(coordinate(cell), direction);
      if (contains(next))
      {
         return cellIndex(next);
      }
      const auto& size = _settings._size;
      // an axis of length 1 is flat, not a ring: a 2d grid must not become its own neighbor along y
      const auto axis_length = (direction == Direction::PositiveX || direction == Direction::NegativeX)   ? size.x
                               : (direction == Direction::PositiveY || direction == Direction::NegativeY) ? size.y
                                                                                                          : size.z;
      if (!_settings._periodic || axis_length == 1)
      {
         return std::nullopt;
      }
      next.x = (next.x + size.x) % size.x;
      next.y = (next.y + size.y) % size.y;
      next.z = (next.z + size.z) % size.z;
      return cellIndex(next);
   }

   [[nodiscard]] static int32_t threadCount(const Settings& settings) noexcept
   {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
      (void)settings;
      return 1;  // a browser without SharedArrayBuffer has no threads
#else
      if (settings._threads > 0)
      {
         return settings._threads;
      }
      return std::max(1, static_cast<int32_t>(std::thread::hardware_concurrency()));
#endif
   }

   [[nodiscard]] int32_t threadCount() const noexcept
   {
      return threadCount(_settings);
   }

   // ------------------------------------------------------------------------------------------------
   // bit sets
   // ------------------------------------------------------------------------------------------------

   [[nodiscard]] bool has(std::size_t cell, TileId tile) const noexcept
   {
      return (_state._domains[cell * _tables->_words + tile / 64] >> (tile % 64) & 1u) != 0;
   }

   [[nodiscard]] TileId firstTile(std::size_t cell) const noexcept
   {
      const auto words = _tables->_words;
      for (std::size_t word = 0; word < words; ++word)
      {
         if (const auto bits = _state._domains[cell * words + word]; bits != 0)
         {
            return static_cast<TileId>(word * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
         }
      }
      return 0;
   }

   [[nodiscard]] double entropyKey(std::size_t cell) const noexcept
   {
      const auto weight_sum = _state._weight_sums[cell];
      // shannon entropy of the weighted distribution, plus a little noise that breaks ties without favoring a corner
      return std::log(weight_sum) - _state._weight_log_sums[cell] / weight_sum + _noise[cell];
   }

   void pushHeap(std::size_t cell)
   {
      // before the first attempt there is no noise and no heap yet; beginAttempt() builds it from scratch
      if (_noise.empty())
      {
         return;
      }
      // the heap keeps outdated entries and skips them when they come up; it is rebuilt before it grows too much
      if (_heap.size() > 8 * _tables->_cell_count + 64)
      {
         rebuildHeap();
         return;
      }
      _heap.push_back({entropyKey(cell), static_cast<uint32_t>(cell)});
      std::push_heap(_heap.begin(), _heap.end());
   }

   void rebuildHeap()
   {
      _heap.clear();
      for (std::size_t cell = 0; cell < _tables->_cell_count; ++cell)
      {
         if (_state._counts[cell] > 1)
         {
            _heap.push_back({entropyKey(cell), static_cast<uint32_t>(cell)});
         }
      }
      std::make_heap(_heap.begin(), _heap.end());
   }

   // ------------------------------------------------------------------------------------------------
   // removing and restoring tiles
   // ------------------------------------------------------------------------------------------------

   /// \brief removes a tile from a cell, records it for undo and lowers the support of the tiles it allowed in the
   ///        neighbors; tiles whose support drops to zero are queued in _pending. false when the cell is left empty.
   bool remove(std::size_t cell, TileId tile)
   {
      const auto& t = *_tables;
      auto& word = _state._domains[cell * t._words + tile / 64];
      const auto bit = uint64_t{1} << (tile % 64);
      if ((word & bit) == 0)
      {
         return true;
      }
      word &= ~bit;
      const auto count = --_state._counts[cell];
      _state._weight_sums[cell] -= t._weights[tile];
      _state._weight_log_sums[cell] -= t._weight_log_weights[tile];
      if (_record_trail)
      {
         _trail.push_back({static_cast<uint32_t>(cell), tile});
      }

      for (std::size_t direction = 0; direction < 6; ++direction)
      {
         const auto next = t._neighbors[cell * 6 + direction];
         if (next == no_neighbor)
         {
            continue;
         }
         const auto begin = t._affected_start[tile * 6 + direction];
         const auto end = t._affected_start[tile * 6 + direction + 1];
         for (auto position = begin; position < end; ++position)
         {
            const auto class_index = t._affected_classes[position];
            if (--_state._supports[next * t._class_count + class_index] != 0)
            {
               continue;
            }
            // nothing on this side supports the class any more: all its tiles leave the neighbor
            for (auto member = t._class_members_start[class_index]; member < t._class_members_start[class_index + 1]; ++member)
            {
               if (const auto other = t._class_members[member]; has(next, other))
               {
                  _pending.push_back({next, other});
               }
            }
         }
      }

      if (count == 1)
      {
         ++_state._collapsed_count;
         if (_collapse_callback)
         {
            _collapse_callback(coordinate(cell), firstTile(cell));
         }
      }
      else if (count == 0)
      {
         --_state._collapsed_count;
         return false;
      }
      else
      {
         pushHeap(cell);
      }
      return true;
   }

   /// \brief removes everything _pending asks for. false on a contradiction.
   bool propagate()
   {
      while (!_pending.empty())
      {
         const auto removal = _pending.back();
         _pending.pop_back();
         if (!remove(removal._cell, removal._tile))
         {
            _pending.clear();
            return false;
         }
      }
      return true;
   }

   bool collapse(std::size_t cell, TileId chosen)
   {
      const auto words = _tables->_words;
      for (std::size_t word = 0; word < words; ++word)
      {
         for (auto bits = _state._domains[cell * words + word]; bits != 0; bits &= bits - 1)
         {
            const auto tile = static_cast<TileId>(word * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
            if (tile != chosen && !remove(cell, tile))
            {
               _pending.clear();
               return false;
            }
         }
      }
      return propagate();
   }

   /// \brief puts back every tile removed after the trail had the given size, newest first.
   void undo(std::size_t trail_size)
   {
      const auto& t = *_tables;
      while (_trail.size() > trail_size)
      {
         const auto [cell, tile] = _trail.back();
         _trail.pop_back();
         _state._domains[cell * t._words + tile / 64] |= uint64_t{1} << (tile % 64);
         const auto count = ++_state._counts[cell];
         _state._weight_sums[cell] += t._weights[tile];
         _state._weight_log_sums[cell] += t._weight_log_weights[tile];
         for (std::size_t direction = 0; direction < 6; ++direction)
         {
            const auto next = t._neighbors[cell * 6 + direction];
            if (next == no_neighbor)
            {
               continue;
            }
            const auto begin = t._affected_start[tile * 6 + direction];
            const auto end = t._affected_start[tile * 6 + direction + 1];
            for (auto position = begin; position < end; ++position)
            {
               ++_state._supports[next * t._class_count + t._affected_classes[position]];
            }
         }
         if (count == 1)
         {
            ++_state._collapsed_count;
         }
         else if (count == 2)
         {
            --_state._collapsed_count;
         }
         if (count > 1)
         {
            pushHeap(cell);
         }
      }
   }

   // ------------------------------------------------------------------------------------------------
   // attempts
   // ------------------------------------------------------------------------------------------------

   /// \brief the first step propagates the constraints set so far and remembers the result for restarts.
   bool start()
   {
      if (_initial)
      {
         return _initial_consistent;
      }
      // tiles that have no compatible tile at all in some direction can never stay
      const auto& t = *_tables;
      for (std::size_t cell = 0; cell < t._cell_count; ++cell)
      {
         for (std::size_t class_index = 0; class_index < t._class_count; ++class_index)
         {
            if (_state._supports[cell * t._class_count + class_index] != 0)
            {
               continue;
            }
            for (auto member = t._class_members_start[class_index]; member < t._class_members_start[class_index + 1]; ++member)
            {
               if (const auto tile = t._class_members[member]; has(cell, tile))
               {
                  _pending.push_back({static_cast<uint32_t>(cell), tile});
               }
            }
         }
      }
      _initial_consistent = propagate();
      _initial = _state;
      _trail.clear();
      _record_trail = _settings._max_backtracks > 0;
      beginAttempt(0);
      return _initial_consistent;
   }

   /// \brief resets to the initial state with the seed of the given attempt; attempt 0 uses Settings::_seed as is.
   void beginAttempt(int32_t attempt)
   {
      _state = *_initial;
      _attempt = attempt;
      _statistics = {._restarts = attempt};
      _decisions.clear();
      _backjump_streak = 0;
      _backjump_depth = 0;
      _trail.clear();
      _pending.clear();
      _random = Random{_settings._seed + static_cast<uint64_t>(attempt) * 0x9e3779b97f4a7c15ull};
      _noise.resize(_tables->_cell_count);
      for (auto& noise : _noise)
      {
         noise = 1e-6 * _random.unit();
      }
      rebuildHeap();
   }

   /// \return true when the attempt finished the grid, false on a contradiction or a stop request
   bool runAttempt(std::stop_token stop)
   {
      while (!stop.stop_requested())
      {
         const auto result = step();
         if (!result)
         {
            return false;
         }
         if (!*result)
         {
            return true;
         }
      }
      return false;
   }

   /// \brief attempts 0 .. max_restarts on a pool of threads. a success cancels every higher attempt; lower ones keep
   ///        running, because they would win. so the result does not depend on the timing.
   std::expected<void, Error> runParallel(int32_t thread_count, int32_t first_attempt)
   {
      const auto attempt_count = _settings._max_restarts + 1;
      std::vector<std::stop_source> stops(static_cast<std::size_t>(attempt_count));
      std::atomic<int32_t> next_attempt{first_attempt};
      std::atomic<int32_t> best_attempt{attempt_count};
      std::mutex mutex;
      std::optional<Solver> winner;

      // the copies run without callbacks: they would be called from other threads
      auto prototype = *this;
      prototype._collapse_callback = nullptr;
      prototype._reset_callback = nullptr;

      {
         std::vector<std::jthread> pool;
         for (auto worker = 0; worker < std::min(thread_count, attempt_count - first_attempt); ++worker)
         {
            pool.emplace_back(
               [&](std::stop_token pool_stop)
               {
                  while (!pool_stop.stop_requested())
                  {
                     const auto attempt = next_attempt++;
                     if (attempt >= attempt_count || attempt > best_attempt.load())
                     {
                        return;
                     }
                     auto solver = prototype;
                     solver.beginAttempt(attempt);
                     if (!solver.runAttempt(stops[static_cast<std::size_t>(attempt)].get_token()))
                     {
                        continue;
                     }
                     const std::scoped_lock lock{mutex};
                     if (attempt < best_attempt.load())
                     {
                        best_attempt = attempt;
                        winner = std::move(solver);
                        for (auto later = attempt + 1; later < attempt_count; ++later)
                        {
                           stops[static_cast<std::size_t>(later)].request_stop();
                        }
                     }
                  }
               }
            );
         }
      }  // the jthreads join here

      if (!winner)
      {
         beginAttempt(_settings._max_restarts);
         return std::unexpected{Error::Contradiction};
      }
      auto collapse_callback = std::move(_collapse_callback);
      auto reset_callback = std::move(_reset_callback);
      *this = std::move(*winner);
      _collapse_callback = std::move(collapse_callback);
      _reset_callback = std::move(reset_callback);
      return {};
   }

   // ------------------------------------------------------------------------------------------------
   // observation
   // ------------------------------------------------------------------------------------------------

   /// \brief the open cell with the lowest entropy, from the heap; outdated entries are skipped.
   [[nodiscard]] std::size_t pickCell()
   {
      while (!_heap.empty())
      {
         std::pop_heap(_heap.begin(), _heap.end());
         const auto entry = _heap.back();
         _heap.pop_back();
         if (_state._counts[entry._cell] > 1 && entry._key == entropyKey(entry._cell))
         {
            return entry._cell;
         }
      }
      // every entry was outdated, which only happens after heavy backtracking
      rebuildHeap();
      return pickCell();
   }

   [[nodiscard]] TileId pickTile(std::size_t cell)
   {
      const auto& t = *_tables;
      auto threshold = _random.unit() * _state._weight_sums[cell];
      auto last = TileId{0};
      for (std::size_t word = 0; word < t._words; ++word)
      {
         for (auto bits = _state._domains[cell * t._words + word]; bits != 0; bits &= bits - 1)
         {
            last = static_cast<TileId>(word * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
            threshold -= t._weights[last];
            if (threshold < 0.0)
            {
               return last;
            }
         }
      }
      return last;  // round off can leave a tiny rest
   }

   Settings _settings;
   std::shared_ptr<const Tables> _tables;
   State _state;
   std::optional<State> _initial;  //!< after the first propagation; every attempt starts here
   bool _initial_consistent{true};
   bool _record_trail{true};
   int32_t _attempt{0};
   int32_t _backjump_streak{0};     //!< contradictions since the search last got past _backjump_depth
   std::size_t _backjump_depth{0};  //!< decision depth of the last contradiction
   std::vector<Removal> _trail;     //!< every removal of the attempt, for backtracking
   std::vector<Removal> _pending;   //!< removals that propagation still has to do
   std::vector<Decision> _decisions;
   std::vector<HeapEntry> _heap;
   std::vector<double> _noise;
   Random _random;
   Statistics _statistics;
   CollapseCallback _collapse_callback;
   ResetCallback _reset_callback;
};

}  // namespace tinywfc
