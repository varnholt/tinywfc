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
};

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
      Design{Kind::Road, "road", ground(Lot, Lot, Road, Road), 3.0f, 2},
      Design{Kind::Road, "road curve", ground(Road, Lot, Road, Lot), 1.0f, 4},
      Design{Kind::Road, "t junction", ground(Road, Road, Road, Lot), 0.5f, 4},
      Design{Kind::Road, "crossing", ground(Road, Road, Road, Road), 0.6f, 1},
      Design{Kind::Canal, "canal", ground(Lot, Lot, Canal, Canal), 0.5f, 2},
      Design{Kind::Canal, "canal curve", ground(Canal, Lot, Canal, Lot), 0.25f, 4},
      Design{Kind::Bridge, "bridge", ground(Canal, Canal, Road, Road), 0.6f, 2},
      Design{Kind::Park, "park", ground(Lot, Lot, Lot, Lot), 0.9f, 1},
      Design{Kind::Building, "building", ground(Lot, Lot, Lot, Lot, House), 4.0f, 1},
      Design{Kind::Air, "air", upper(Sky, Sky), 1.0f, 1},
      Design{Kind::Floor, "floor", upper(House, House), 1.3f, 1},
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
         const auto sockets = tinywfc::rotateY(design._sockets, turns);
         const auto id = static_cast<tinywfc::TileId>(set._tiles.size());
         socket_tiles.push_back({sockets, design._weight / static_cast<float>(design._rotations)});
         set._tiles.push_back({design._kind, turns, sidesWith(sockets, Road), sidesWith(sockets, Canal), design._name});

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

std::expected<tinywfc::Solver, tinywfc::Error> makeSolver(const tinywfc::Size& size, uint64_t seed)
{
   if (size.y < 2)
   {
      return std::unexpected{tinywfc::Error::InvalidSize};
   }
   const auto& set = tileset();
   auto solver = tinywfc::Solver::create(set._rules, {._size = size, ._seed = seed});
   if (!solver)
   {
      return solver;
   }

   // ground tiles only on the ground, upper tiles only above, and no open floor under the sky
   for (auto y = 0; y < size.y; ++y)
   {
      for (auto z = 0; z < size.z; ++z)
      {
         for (auto x = 0; x < size.x; ++x)
         {
            const tinywfc::Coordinate cell{x, y, z};
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
