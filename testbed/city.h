#pragma once

// the city tileset of the testbed: roads, canals, bridges, parks and buildings on the ground layer, floors and roofs
// stacked above. it only describes tiles and rules; meshes are built from it in meshes.cpp.

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include <tinywfc/tinywfc.h>

namespace city
{

enum class Kind : uint8_t
{
   Air,
   Road,    //!< straight, curve, t junction or crossing, depending on the road sides
   Canal,   //!< straight or curve
   Bridge,  //!< a road crossing a canal
   Park,
   Building,  //!< the ground floor of a building
   Floor,     //!< one more storey
   FlatRoof,
   GardenRoof,
   PitchedRoof,
};

/// \brief horizontal sides as bits, in the order +x, -x, +z, -z
enum Side : uint8_t
{
   SidePositiveX = 1u << 0,
   SideNegativeX = 1u << 1,
   SidePositiveZ = 1u << 2,
   SideNegativeZ = 1u << 3,
};

struct TileInfo
{
   Kind _kind{Kind::Air};
   int32_t _quarter_turns{0};  //!< rotation around +y, the same one tinywfc::rotateY applies to the sockets
   uint8_t _road_sides{0};     //!< sides a road leaves the tile
   uint8_t _canal_sides{0};    //!< sides a canal leaves the tile
   std::string_view _name;
};

struct Tileset
{
   tinywfc::Rules _rules;
   std::vector<TileInfo> _tiles;
   std::vector<tinywfc::TileId> _ground_tiles;  //!< allowed in layer 0
   std::vector<tinywfc::TileId> _upper_tiles;   //!< allowed above
   std::vector<tinywfc::TileId> _floor_tiles;   //!< banned from the top layer, they need something on top
};

[[nodiscard]] const Tileset& tileset();

/// \brief a solver for a city of size.x * size.z blocks and size.y layers (at least 2), with the layer rules applied.
[[nodiscard]] std::expected<tinywfc::Solver, tinywfc::Error> makeSolver(const tinywfc::Size& size, uint64_t seed);

}  // namespace city
