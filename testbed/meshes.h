#pragma once

// procedural low poly meshes for the city tiles. every tile is one cell: x and z in [-0.5, 0.5], y from the bottom of its
// layer upwards. buildings get their facade color per column at draw time: vertices with tint 1 are multiplied with it.

#include <cstdint>
#include <vector>

#include "city.h"

namespace testbed
{

inline constexpr float storey_height = 0.5f;  //!< height of one layer

struct Vertex
{
   float _x{0.0f};
   float _y{0.0f};
   float _z{0.0f};
   float _r{0.0f};
   float _g{0.0f};
   float _b{0.0f};
   float _tint{0.0f};
};

/// \brief triangles of one tile, already turned to its orientation
[[nodiscard]] std::vector<Vertex> tileMesh(const city::TileInfo& tile);

/// \brief a large ground plane around the city, slightly below the tiles
[[nodiscard]] std::vector<Vertex> groundMesh(float half_width, float half_depth);

/// \brief a tile that looks the same in all four rotations, so the renderer may turn it for variety
[[nodiscard]] bool isSymmetric(const city::TileInfo& tile);

}  // namespace testbed
