#include "city.h"

#include <array>

namespace city
{

namespace
{

using tinywfc::Direction;
using tinywfc::index;

/// \brief what touches a side: two tiles fit when the sockets on their touching sides are equal
enum class Socket : uint8_t
{
   Ground,  //!< below the ground layer; nothing is ever there
   Lot,     //!< the edge of a block without a road or canal: building, park, the side of a road
   Road,
   Canal,
   Sky,    //!< open air above
   House,  //!< the inside of a building continues
   Upper,  //!< sides in the upper layers; anything goes there
   Deck,   //!< an elevated road continues
   // vertical sockets that care about orientation come in one value per rotation: a deck along z needs the pillar of
   // an underpass along x, and a ramp top has to face the same way as the ramp below it
   Pillar0,  //!< a ground tile carries an elevated road along z
   Pillar1,  //!< ... along x
   RampTop0,
   RampTop1,
   RampTop2,
   RampTop3,
};

/// rief the socket of a turned tile on its top or bottom: oriented sockets move on with the rotation
Socket turnVertical(Socket socket, int32_t quarter_turns)
{
   using enum Socket;
   if (socket == Pillar0)
   {
      return static_cast<Socket>(static_cast<int32_t>(Pillar0) + quarter_turns % 2);
   }
   if (socket == RampTop0)
   {
      return static_cast<Socket>(static_cast<int32_t>(RampTop0) + quarter_turns % 4);
   }
   return socket;
}

using Sockets = std::array<Socket, 6>;

/// \brief sockets in Direction order: +x, -x, +y, -y, +z, -z
constexpr Sockets ground(Socket positive_x, Socket negative_x, Socket positive_z, Socket negative_z, Socket top = Socket::Sky)
{
   return {positive_x, negative_x, top, Socket::Ground, positive_z, negative_z};
}

constexpr Sockets upper(Socket top, Socket bottom)
{
   return {Socket::Upper, Socket::Upper, top, bottom, Socket::Upper, Socket::Upper};
}

uint8_t sidesWith(const Sockets& sockets, Socket socket)
{
   const auto side = [&](Direction direction, Side bit) { return sockets[index(direction)] == socket ? static_cast<uint32_t>(bit) : 0u; };
   return static_cast<uint8_t>(
      side(Direction::PositiveX, SidePositiveX) | side(Direction::NegativeX, SideNegativeX) | side(Direction::PositiveZ, SidePositiveZ) |
      side(Direction::NegativeZ, SideNegativeZ)
   );
}

struct Design
{
   Kind _kind;
   std::string_view _name;
   Sockets _sockets;
   float _weight;
   int32_t _rotations;  //!< distinct quarter turns: 1, 2 or 4
};

Tileset build()
{
   using enum Socket;
   const std::array designs{
      Design{Kind::Road, "road", ground(Lot, Lot, Road, Road), 1.6f, 2},
      Design{Kind::Road, "road curve", ground(Road, Lot, Road, Lot), 0.6f, 4},
      Design{Kind::Road, "t junction", ground(Road, Road, Road, Lot), 0.35f, 4},
      Design{Kind::Road, "crossing", ground(Road, Road, Road, Road), 0.3f, 1},
      Design{Kind::Canal, "canal", ground(Lot, Lot, Canal, Canal), 0.5f, 2},
      Design{Kind::Canal, "canal curve", ground(Canal, Lot, Canal, Lot), 0.25f, 4},
      Design{Kind::Bridge, "bridge", ground(Canal, Canal, Road, Road), 0.3f, 2},
      Design{Kind::Park, "park", ground(Lot, Lot, Lot, Lot), 1.8f, 1},
      Design{Kind::Building, "building", ground(Lot, Lot, Lot, Lot, House), 5.0f, 1},
      // the elevated road: pillars stand on ground tiles that offer them, ramps lead up from a street
      Design{Kind::Road, "underpass", ground(Road, Road, Lot, Lot, Pillar0), 0.5f, 2},
      Design{Kind::Park, "park under deck", ground(Lot, Lot, Lot, Lot, Pillar0), 0.2f, 2},
      Design{Kind::Ramp, "ramp", ground(Lot, Lot, Lot, Road, RampTop0), 0.12f, 4},
      Design{Kind::RampTop, "ramp top", {Upper, Upper, Sky, RampTop0, Deck, Upper}, 1.0f, 4},
      Design{Kind::Deck, "deck", {Upper, Upper, Sky, Pillar0, Deck, Deck}, 1.0f, 2},
      Design{Kind::Air, "air", upper(Sky, Sky), 1.0f, 1},
      Design{Kind::Floor, "floor", upper(House, House), 1.5f, 1},
      Design{Kind::FlatRoof, "flat roof", upper(Sky, House), 0.6f, 1},
      Design{Kind::GardenRoof, "garden roof", upper(Sky, House), 0.25f, 1},
      Design{Kind::PitchedRoof, "pitched roof", upper(Sky, House), 0.35f, 2},
   };

   std::vector<tinywfc::SocketTile<Socket>> socket_tiles;
   Tileset set;
   for (const auto& design : designs)
   {
      for (auto turns = 0; turns < design._rotations; ++turns)
      {
         auto sockets = tinywfc::rotateY(design._sockets, turns);
         for (const auto vertical : {Direction::PositiveY, Direction::NegativeY})
         {
            sockets[index(vertical)] = turnVertical(sockets[index(vertical)], turns);
         }
         const auto id = static_cast<tinywfc::TileId>(set._tiles.size());
         socket_tiles.push_back({sockets, design._weight / static_cast<float>(design._rotations)});
         set._tiles.push_back(
            {design._kind,
             turns,
             sidesWith(sockets, Road),
             sidesWith(sockets, Canal),
             sockets[index(Direction::PositiveY)] == Pillar0 || sockets[index(Direction::PositiveY)] == Pillar1,
             design._name}
         );

         const auto is_ground = sockets[index(Direction::NegativeY)] == Ground;
         (is_ground ? set._ground_tiles : set._upper_tiles).push_back(id);
         if (design._kind == Kind::Floor)
         {
            set._floor_tiles.push_back(id);
         }
      }
   }
   set._rules = tinywfc::rulesFromSockets<Socket>(socket_tiles);
   return set;
}

}  // namespace

const Tileset& tileset()
{
   static const auto set = build();
   return set;
}

std::expected<tinywfc::Solver, tinywfc::Error> makeSolver(const tinywfc::Size& size, uint64_t seed, int32_t threads)
{
   if (size.y < 2)
   {
      return std::unexpected{tinywfc::Error::InvalidSize};
   }
   const auto& set = tileset();
   auto solver = tinywfc::Solver::create(set._rules, {._size = size, ._seed = seed, ._threads = threads});
   if (!solver)
   {
      return solver;
   }

   // ground tiles only on the ground, upper tiles only above, no open floor under the sky, observed layer by layer
   for (auto y = 0; y < size.y; ++y)
   {
      for (auto z = 0; z < size.z; ++z)
      {
         for (auto x = 0; x < size.x; ++x)
         {
            const tinywfc::Coordinate cell{x, y, z};
            // the city grows from the ground up: streets and lots first, so a building's height comes from the
            // stacking rules and not from a roof that happened to be decided high up early
            (void)solver->setPriority(cell, static_cast<double>(y) * 100.0);
            if (const auto restricted = solver->restrict(cell, y == 0 ? set._ground_tiles : set._upper_tiles); !restricted)
            {
               return std::unexpected{restricted.error()};
            }
            if (y == size.y - 1)
            {
               for (const auto floor : set._floor_tiles)
               {
                  if (const auto banned = solver->ban(cell, floor); !banned)
                  {
                     return std::unexpected{banned.error()};
                  }
               }
            }
         }
      }
   }
   return solver;
}

}  // namespace city
