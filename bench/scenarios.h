#pragma once

// rule sets for the benchmark

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <tinywfc/tinywfc.h>

namespace bench
{

/// \brief roads, canals, bridges and parks: the ground layer of the testbed's city. many tiles fit everywhere, so it
///        rarely contradicts; the cost is mostly propagation.
inline tinywfc::Rules roads()
{
   enum class Socket : uint8_t
   {
      Flat,
      Lot,
      Road,
      Canal,
   };
   using enum Socket;
   // +x, -x, +y, -y, +z, -z
   struct Design
   {
      std::array<Socket, 6> _sockets;
      float _weight;
      int32_t _rotations;
   };
   const std::array designs{
      Design{{Lot, Lot, Flat, Flat, Road, Road}, 3.0f, 2},
      Design{{Road, Lot, Flat, Flat, Road, Lot}, 1.0f, 4},
      Design{{Road, Road, Flat, Flat, Road, Lot}, 0.5f, 4},
      Design{{Road, Road, Flat, Flat, Road, Road}, 0.6f, 1},
      Design{{Lot, Lot, Flat, Flat, Canal, Canal}, 0.5f, 2},
      Design{{Canal, Lot, Flat, Flat, Canal, Lot}, 0.25f, 4},
      Design{{Canal, Canal, Flat, Flat, Road, Road}, 0.6f, 2},
      Design{{Lot, Lot, Flat, Flat, Lot, Lot}, 4.0f, 1},
   };
   std::vector<tinywfc::SocketTile<Socket>> tiles;
   for (const auto& design : designs)
   {
      for (auto turns = 0; turns < design._rotations; ++turns)
      {
         tiles.push_back({tinywfc::rotateY(design._sockets, turns), design._weight});
      }
   }
   return tinywfc::rulesFromSockets<Socket>(tiles);
}

/// \brief three colors, neighbors always different: easy to start, easy to paint into a corner. measures how well
///        contradictions are handled.
inline tinywfc::Rules threeColors()
{
   tinywfc::Rules rules{3};
   for (tinywfc::TileId a = 0; a < 3; ++a)
   {
      for (tinywfc::TileId b = 0; b < 3; ++b)
      {
         if (a != b)
         {
            for (const auto direction : tinywfc::directions)
            {
               rules.allow(a, direction, b);
            }
         }
      }
   }
   return rules;
}

/// \brief 3d pipes: every combination of the six sides with or without a pipe, 64 tiles. pipes must meet pipes.
inline tinywfc::Rules pipes()
{
   std::vector<tinywfc::SocketTile<bool>> tiles;
   for (auto mask = 0; mask < 64; ++mask)
   {
      tinywfc::SocketTile<bool> tile;
      auto openings = 0;
      for (std::size_t side = 0; side < 6; ++side)
      {
         tile._sockets[side] = (mask >> side & 1) != 0;
         openings += tile._sockets[side] ? 1 : 0;
      }
      // mostly empty space and straight runs, few junctions
      tile._weight = openings == 0 ? 8.0f : openings == 2 ? 1.0f : 0.2f;
      tiles.push_back(tile);
   }
   return tinywfc::rulesFromSockets<bool>(tiles);
}

struct Scenario
{
   std::string_view _name;
   tinywfc::Rules (*_rules)();
   tinywfc::Size _size;
   bool _two_dimensional;
};

inline const std::array<Scenario, 6> scenarios{{
   {"roads 32^2", &roads, {32, 1, 32}, true},
   {"roads 128^2", &roads, {128, 1, 128}, true},
   {"three colors 32^2", &threeColors, {32, 1, 32}, true},
   {"three colors 128^2", &threeColors, {128, 1, 128}, true},
   {"pipes 32^3", &pipes, {32, 32, 32}, false},
   {"three colors 32^3", &threeColors, {32, 32, 32}, false},
}};

}  // namespace bench
