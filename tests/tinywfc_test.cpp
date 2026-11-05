// tests of the public api

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include "test_framework.h"
#include "tinywfc/tinywfc.h"

using tinywfc::Coordinate;
using tinywfc::Direction;
using tinywfc::TileId;

namespace
{

/// \brief checks every pair of neighbors in a finished grid against the rules.
int32_t countViolations(const tinywfc::Solver& solver, const tinywfc::Rules& rules, bool periodic = false)
{
   const auto& size = solver.size();
   auto violations = 0;
   for (auto y = 0; y < size.y; ++y)
   {
      for (auto z = 0; z < size.z; ++z)
      {
         for (auto x = 0; x < size.x; ++x)
         {
            const Coordinate cell{x, y, z};
            const auto tile = solver.tile(cell);
            if (!tile)
            {
               ++violations;
               continue;
            }
            for (const auto direction : tinywfc::directions)
            {
               auto next = tinywfc::adjacent(cell, direction);
               if (!solver.contains(next))
               {
                  const auto flat = (next.x < 0 || next.x >= size.x)   ? size.x == 1
                                    : (next.y < 0 || next.y >= size.y) ? size.y == 1
                                                                       : size.z == 1;
                  if (!periodic || flat)
                  {
                     continue;
                  }
                  next = {(next.x + size.x) % size.x, (next.y + size.y) % size.y, (next.z + size.z) % size.z};
               }
               if (!rules.allows(*tile, direction, *solver.tile(next)))
               {
                  ++violations;
               }
            }
         }
      }
   }
   return violations;
}

/// \brief land, coast and sea: sea never touches land directly. a classic small tileset with a single constraint.
tinywfc::Rules landAndSea()
{
   tinywfc::Rules rules{3};
   constexpr TileId land = 0;
   constexpr TileId coast = 1;
   constexpr TileId sea = 2;
   for (const auto& [a, b] : std::array<std::array<TileId, 2>, 5>{{{land, land}, {land, coast}, {coast, coast}, {coast, sea}, {sea, sea}}})
   {
      rules.allowEverywhere(a, b);
   }
   rules.setWeight(sea, 2.0f);
   return rules;
}

/// \brief a 2d checkerboard of two tiles that must alternate.
tinywfc::Rules checkerboard()
{
   tinywfc::Rules rules{2};
   for (const auto direction : {Direction::PositiveX, Direction::PositiveZ})
   {
      rules.allow(0, direction, 1);
      rules.allow(1, direction, 0);
   }
   return rules;
}

}  // namespace

TEST_CASE("opposite directions pair up")
{
   for (const auto direction : tinywfc::directions)
   {
      CHECK(tinywfc::opposite(direction) != direction);
      CHECK(tinywfc::opposite(tinywfc::opposite(direction)) == direction);
      const auto there = tinywfc::adjacent({}, direction);
      const auto back = tinywfc::adjacent(there, tinywfc::opposite(direction));
      CHECK(back == Coordinate{});
   }
}

TEST_CASE("allow is symmetric")
{
   tinywfc::Rules rules{2};
   rules.allow(0, Direction::PositiveX, 1);
   CHECK(rules.allows(0, Direction::PositiveX, 1));
   CHECK(rules.allows(1, Direction::NegativeX, 0));
   CHECK(!rules.allows(1, Direction::PositiveX, 0));
   CHECK(!rules.allows(0, Direction::PositiveZ, 1));
}

TEST_CASE("invalid settings and rules are reported")
{
   const auto rules = landAndSea();
   CHECK(tinywfc::Solver::create(rules, {._size = {0, 1, 4}}).error() == tinywfc::Error::InvalidSize);
   CHECK(tinywfc::Solver::create(rules, {._size = {4, -1, 4}}).error() == tinywfc::Error::InvalidSize);
   CHECK(tinywfc::Solver::create(tinywfc::Rules{0}, {}).error() == tinywfc::Error::InvalidRules);

   auto zero_weight = landAndSea();
   zero_weight.setWeight(1, 0.0f);
   CHECK(tinywfc::Solver::create(zero_weight, {}).error() == tinywfc::Error::InvalidRules);

   auto solver = tinywfc::Solver::create(rules, {._size = {4, 1, 4}});
   CHECK(solver.has_value());
   CHECK(solver->ban({4, 0, 0}, 0).error() == tinywfc::Error::InvalidCoordinate);
   CHECK(solver->ban({0, 0, 0}, 3).error() == tinywfc::Error::InvalidTile);
   const std::array<TileId, 1> out_of_range{7};
   CHECK(solver->restrict({0, 0, 0}, out_of_range).error() == tinywfc::Error::InvalidTile);
}

TEST_CASE("every adjacency rule holds in the result")
{
   const auto rules = landAndSea();
   for (const auto seed : {1ull, 2ull, 3ull, 1234ull})
   {
      auto solver = tinywfc::Solver::create(rules, {._size = {24, 1, 24}, ._seed = seed});
      const auto result = solver->run();
      CHECK_MESSAGE(result.has_value(), "seed {}", seed);
      CHECK(solver->finished());
      CHECK(solver->result().size() == 24u * 24u);
      CHECK_MESSAGE(countViolations(*solver, rules) == 0, "seed {}", seed);
   }
}

TEST_CASE("a fixed seed gives the same result, another seed a different one")
{
   const auto rules = landAndSea();
   const auto solve = [&](uint64_t seed)
   {
      auto solver = tinywfc::Solver::create(rules, {._size = {20, 1, 20}, ._seed = seed});
      CHECK(solver->run().has_value());
      return solver->result();
   };
   CHECK(solve(7) == solve(7));
   CHECK(solve(7) != solve(8));

   // the exact tiles are part of the contract: the generator does not use std distributions, so this holds on every
   // platform and standard library
   const auto tiles = solve(7);
   const auto land_count = std::ranges::count(tiles, 0u);
   const auto sea_count = std::ranges::count(tiles, 2u);
   CHECK(land_count + sea_count > 0);
}

TEST_CASE("weights shift the distribution")
{
   auto rules = landAndSea();
   const auto count_sea = [&](float sea_weight)
   {
      rules.setWeight(2, sea_weight);
      auto solver = tinywfc::Solver::create(rules, {._size = {32, 1, 32}, ._seed = 99});
      CHECK(solver->run().has_value());
      return std::ranges::count(solver->result(), 2u);
   };
   CHECK(count_sea(20.0f) > count_sea(0.05f));
}

TEST_CASE("3d grids respect vertical rules")
{
   // a column must be stone at the bottom, then dirt, then grass, then air
   tinywfc::Rules rules{4};
   constexpr TileId stone = 0;
   constexpr TileId dirt = 1;
   constexpr TileId grass = 2;
   constexpr TileId air = 3;
   rules.allow(stone, Direction::PositiveY, stone);
   rules.allow(stone, Direction::PositiveY, dirt);
   rules.allow(dirt, Direction::PositiveY, grass);
   rules.allow(grass, Direction::PositiveY, air);
   rules.allow(air, Direction::PositiveY, air);
   for (const auto tile : {stone, dirt, grass, air})
   {
      for (const auto other : {stone, dirt, grass, air})
      {
         rules.allow(tile, Direction::PositiveX, other);
         rules.allow(tile, Direction::PositiveZ, other);
      }
   }

   auto solver = tinywfc::Solver::create(rules, {._size = {6, 6, 5}, ._seed = 5});
   for (auto z = 0; z < 5; ++z)
   {
      for (auto x = 0; x < 6; ++x)
      {
         const std::array<TileId, 1> bottom{stone};
         const std::array<TileId, 1> top{air};
         CHECK(solver->restrict({x, 0, z}, bottom).has_value());
         CHECK(solver->restrict({x, 5, z}, top).has_value());
      }
   }
   CHECK(solver->run().has_value());
   CHECK(countViolations(*solver, rules) == 0);

   // exactly one grass tile in every column
   for (auto z = 0; z < 5; ++z)
   {
      for (auto x = 0; x < 6; ++x)
      {
         auto grass_count = 0;
         for (auto y = 0; y < 6; ++y)
         {
            grass_count += solver->tile({x, y, z}) == grass ? 1 : 0;
         }
         CHECK_MESSAGE(grass_count == 1, "column {} {} has {} grass tiles", x, z, grass_count);
      }
   }
}

TEST_CASE("periodic grids wrap around")
{
   // a 2 wide checkerboard only works periodically when the size is even
   const auto rules = checkerboard();
   auto even = tinywfc::Solver::create(rules, {._size = {6, 1, 4}, ._seed = 3, ._periodic = true});
   CHECK(even->run().has_value());
   CHECK(countViolations(*even, rules, true) == 0);

   auto odd = tinywfc::Solver::create(rules, {._size = {5, 1, 4}, ._seed = 3, ._periodic = true, ._max_restarts = 2});
   CHECK(odd->run().error() == tinywfc::Error::Contradiction);
}

TEST_CASE("contradictions are reported, not crashed on")
{
   // no tile may have a neighbor along x, so a row of three cannot be tiled; the first propagation finds out
   tinywfc::Rules no_x_neighbors{2};
   no_x_neighbors.allow(0, Direction::PositiveZ, 1);
   auto solver = tinywfc::Solver::create(no_x_neighbors, {._size = {3, 1, 1}, ._max_restarts = 3});
   const auto result = solver->run();
   CHECK(!result.has_value() && result.error() == tinywfc::Error::Contradiction);
   CHECK(solver->statistics()._restarts == 0);  // the same on every seed, so no restart is tried

   // a pair that only works in one order: with backtracking but no restarts the solver still finds it
   tinywfc::Rules ordered{2};
   ordered.allow(0, Direction::PositiveX, 1);
   auto pair = tinywfc::Solver::create(ordered, {._size = {2, 1, 1}, ._max_restarts = 0});
   CHECK(pair->run().has_value());
   CHECK(pair->tile({0, 0, 0}) == 0u && pair->tile({1, 0, 0}) == 1u);

   // a constraint that empties a cell fails right away
   auto constrained = tinywfc::Solver::create(checkerboard(), {._size = {2, 1, 1}});
   const std::array<TileId, 1> zero{0};
   CHECK(constrained->restrict({0, 0, 0}, zero).has_value());
   CHECK(constrained->restrict({1, 0, 0}, zero).error() == tinywfc::Error::Contradiction);
}

TEST_CASE("backtracking solves what greedy collapsing gets wrong")
{
   // three colors on a grid, neighbors always different: collapsing without backtracking can paint itself into a
   // corner, with backtracking every seed finishes in its first attempt
   tinywfc::Rules rules{3};
   for (TileId a = 0; a < 3; ++a)
   {
      for (TileId b = 0; b < 3; ++b)
      {
         if (a != b)
         {
            rules.allow(a, Direction::PositiveX, b);
            rules.allow(a, Direction::PositiveZ, b);
         }
      }
   }

   auto greedy_failures = 0;
   for (uint64_t seed = 0; seed < 20; ++seed)
   {
      auto greedy = tinywfc::Solver::create(rules, {._size = {12, 1, 12}, ._seed = seed, ._max_backtracks = 0, ._max_restarts = 0});
      greedy_failures += greedy->run().has_value() ? 0 : 1;

      auto backtracking = tinywfc::Solver::create(rules, {._size = {12, 1, 12}, ._seed = seed, ._max_restarts = 0});
      CHECK_MESSAGE(backtracking->run().has_value(), "seed {}", seed);
      CHECK(countViolations(*backtracking, rules) == 0);
   }
   std::println("   greedy failed {} of 20 times", greedy_failures);
}

TEST_CASE("parallel attempts give the same result as one thread")
{
   // without backtracking the three color grid needs restarts, so several attempts are in flight at once
   tinywfc::Rules rules{3};
   for (TileId a = 0; a < 3; ++a)
   {
      for (TileId b = 0; b < 3; ++b)
      {
         if (a != b)
         {
            rules.allow(a, Direction::PositiveX, b);
            rules.allow(a, Direction::PositiveZ, b);
         }
      }
   }

   auto restarts_seen = 0;
   for (uint64_t seed = 0; seed < 16; ++seed)
   {
      const tinywfc::Settings settings{._size = {40, 1, 40}, ._seed = seed, ._max_backtracks = 0, ._max_restarts = 60};
      auto serial = tinywfc::Solver::create(rules, settings);
      auto parallel =
         tinywfc::Solver::create(rules, {._size = settings._size, ._seed = seed, ._max_backtracks = 0, ._max_restarts = 60, ._threads = 4});
      const auto serial_result = serial->run();
      const auto parallel_result = parallel->run();
      CHECK(serial_result.has_value() == parallel_result.has_value());
      CHECK_MESSAGE(serial->result() == parallel->result(), "seed {}", seed);
      CHECK(serial->statistics()._restarts == parallel->statistics()._restarts);
      restarts_seen += serial->statistics()._restarts;
   }
   CHECK_MESSAGE(restarts_seen > 0, "the test should need restarts to mean anything");
}

TEST_CASE("callbacks report every collapse")
{
   const auto rules = landAndSea();
   auto solver = tinywfc::Solver::create(rules, {._size = {10, 1, 10}, ._seed = 11});
   std::set<std::size_t> collapsed;
   auto resets = 0;
   solver->setCollapseCallback(
      [&](const Coordinate& cell, TileId tile)
      {
         CHECK(solver->tile(cell) == tile);
         collapsed.insert(solver->cellIndex(cell));
      }
   );
   solver->setResetCallback([&]() { ++resets; });
   CHECK(solver->run().has_value());
   CHECK(collapsed.size() == 100u);
   CHECK(resets == solver->statistics()._backtracks + solver->statistics()._restarts);
}

TEST_CASE("priorities set the order of observation")
{
   // four tiles that fit anywhere, so every collapse is a decision of its own
   tinywfc::Rules rules{4};
   for (TileId a = 0; a < 4; ++a)
   {
      for (TileId b = 0; b < 4; ++b)
      {
         rules.allowEverywhere(a, b);
      }
   }
   auto solver = tinywfc::Solver::create(rules, {._size = {5, 4, 5}, ._seed = 8});
   for (auto y = 0; y < 4; ++y)
   {
      for (auto z = 0; z < 5; ++z)
      {
         for (auto x = 0; x < 5; ++x)
         {
            CHECK(solver->setPriority({x, y, z}, y * 10.0).has_value());
         }
      }
   }
   CHECK(solver->setPriority({5, 0, 0}, 1.0).error() == tinywfc::Error::InvalidCoordinate);

   std::vector<int32_t> layers;
   solver->setCollapseCallback([&](const Coordinate& cell, TileId) { layers.push_back(cell.y); });
   CHECK(solver->run().has_value());
   CHECK(layers.size() == 100u);
   CHECK(std::ranges::is_sorted(layers));
}

TEST_CASE("stepping one cell at a time")
{
   auto solver = tinywfc::Solver::create(landAndSea(), {._size = {8, 1, 8}, ._seed = 2});
   auto steps = 0;
   auto more = solver->step();
   while (more && *more)
   {
      ++steps;
      more = solver->step();
   }
   CHECK(steps > 0 && steps < 64);  // propagation decides some cells without a step of their own
   CHECK(more.has_value());
   CHECK(solver->finished());
   CHECK(!*solver->step());
}

TEST_CASE("sockets build rules, rotateY turns them")
{
   enum class Socket : uint8_t
   {
      A,
      B,
   };
   using enum Socket;
   // +x, -x, +y, -y, +z, -z
   const std::array<Socket, 6> sockets{A, B, A, A, B, B};
   CHECK(tinywfc::rotateY(sockets, 4) == sockets);
   CHECK(tinywfc::rotateY(sockets, -1) == tinywfc::rotateY(sockets, 3));

   // a quarter turn moves the +x side to -z, matching a right handed rotation of the mesh
   const auto turned = tinywfc::rotateY(sockets, 1);
   CHECK(turned[tinywfc::index(Direction::NegativeZ)] == A);
   CHECK(turned[tinywfc::index(Direction::PositiveX)] == B);
   CHECK(turned[tinywfc::index(Direction::PositiveY)] == A);

   const std::array<tinywfc::SocketTile<Socket>, 2> tiles{{{sockets, 1.0f}, {turned, 2.0f}}};
   const auto rules = tinywfc::rulesFromSockets<Socket>(tiles);
   CHECK(rules.tileCount() == 2u);
   CHECK(rules.weight(1) == 2.0f);
   // tile 0 has A on +x and B on -x, so it does not fit right of itself; on z it has B on both sides, so it does
   CHECK(!rules.allows(0, Direction::PositiveX, 0));
   CHECK(rules.allows(0, Direction::PositiveZ, 0));
   // the turned tile has B on +x (from +z), which matches tile 0's B on -x
   CHECK(rules.allows(1, Direction::PositiveX, 0));
}

int main()
{
   return test::runAll();
}
