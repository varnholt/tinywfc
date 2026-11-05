#include "meshes.h"

#include <array>
#include <cmath>
#include <numbers>

namespace testbed
{

namespace
{

struct Color
{
   float _r;
   float _g;
   float _b;
};

constexpr Color asphalt{0.23f, 0.24f, 0.27f};
constexpr Color sidewalk{0.70f, 0.69f, 0.66f};
constexpr Color marking{0.92f, 0.90f, 0.82f};
constexpr Color water{0.25f, 0.52f, 0.72f};
constexpr Color stone{0.58f, 0.55f, 0.50f};
constexpr Color grass{0.43f, 0.62f, 0.33f};
constexpr Color trunk{0.42f, 0.30f, 0.20f};
constexpr Color leaves{0.26f, 0.50f, 0.27f};
constexpr Color facade{1.0f, 1.0f, 1.0f};  // tinted per building
constexpr Color glass{0.20f, 0.27f, 0.36f};
constexpr Color door{0.36f, 0.24f, 0.17f};
constexpr Color roof_slab{0.55f, 0.55f, 0.57f};
constexpr Color roof_tiles{0.70f, 0.33f, 0.24f};
constexpr Color concrete{0.66f, 0.66f, 0.68f};
constexpr Color railing{0.86f, 0.86f, 0.88f};

constexpr float slab_bottom = -0.06f;
constexpr float road_half_width = 0.3f;
constexpr float building_half_width = 0.42f;

class Builder
{
public:
   void
   triangle(const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 3>& c, Color color, float tint = 0.0f)
   {
      for (const auto& point : {a, b, c})
      {
         _vertices.push_back({point[0], point[1], point[2], color._r, color._g, color._b, tint});
      }
   }

   void quad(
      const std::array<float, 3>& a,
      const std::array<float, 3>& b,
      const std::array<float, 3>& c,
      const std::array<float, 3>& d,
      Color color,
      float tint = 0.0f
   )
   {
      triangle(a, b, c, color, tint);
      triangle(a, c, d, color, tint);
   }

   /// \brief an axis aligned box
   void box(float x0, float y0, float z0, float x1, float y1, float z1, Color color, float tint = 0.0f)
   {
      quad({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, color, tint);  // top
      quad({x0, y0, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, color, tint);  // bottom
      quad({x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, color, tint);  // +z
      quad({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, color, tint);  // -z
      quad({x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, color, tint);  // +x
      quad({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, color, tint);  // -x
   }

   /// \brief a cone with a few sides, for tree crowns
   void cone(float x, float y, float z, float radius, float height, Color color)
   {
      constexpr auto sides = 7;
      for (auto side = 0; side < sides; ++side)
      {
         const auto a0 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(side) / sides;
         const auto a1 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(side + 1) / sides;
         const std::array p0{x + radius * std::cos(a0), y, z + radius * std::sin(a0)};
         const std::array p1{x + radius * std::cos(a1), y, z + radius * std::sin(a1)};
         triangle(p0, p1, {x, y + height, z}, color);
         triangle(p0, {x, y, z}, p1, color);
      }
   }

   void tree(float x, float z, float y = 0.0f, float scale = 1.0f)
   {
      box(x - 0.025f * scale, y, z - 0.025f * scale, x + 0.025f * scale, y + 0.1f * scale, z + 0.025f * scale, trunk);
      cone(x, y + 0.08f * scale, z, 0.13f * scale, 0.32f * scale, leaves);
   }

   /// \brief turns everything built so far by quarter turns around +y, the same rotation tinywfc::rotateY applies
   void turn(int32_t quarter_turns)
   {
      const auto angle = static_cast<float>(quarter_turns) * std::numbers::pi_v<float> * 0.5f;
      const auto c = std::round(std::cos(angle));
      const auto s = std::round(std::sin(angle));
      for (auto& vertex : _vertices)
      {
         const auto x = vertex._x;
         const auto z = vertex._z;
         vertex._x = c * x + s * z;
         vertex._z = -s * x + c * z;
      }
   }

   [[nodiscard]] std::vector<Vertex> take()
   {
      return std::move(_vertices);
   }

private:
   std::vector<Vertex> _vertices;
};

/// \brief the rectangle from the tile center to one side, half_width wide: +x, -x, +z, -z
struct Arm
{
   city::Side _side;
   float _x0;
   float _z0;
   float _x1;
   float _z1;
};

std::array<Arm, 4> arms(float half_width)
{
   return {{
      {city::SidePositiveX, half_width, -half_width, 0.5f, half_width},
      {city::SideNegativeX, -0.5f, -half_width, -half_width, half_width},
      {city::SidePositiveZ, -half_width, half_width, half_width, 0.5f},
      {city::SideNegativeZ, -half_width, -0.5f, half_width, -half_width},
   }};
}

void slab(Builder& builder, Color color)
{
   builder.box(-0.5f, slab_bottom, -0.5f, 0.5f, 0.0f, 0.5f, color);
}

/// \brief asphalt from the center to every road side, with dashed center lines
void roads(Builder& builder, uint8_t sides, float y)
{
   builder.box(-road_half_width, y, -road_half_width, road_half_width, y + 0.012f, road_half_width, asphalt);
   const auto top = y + 0.012f;
   for (const auto& arm : arms(road_half_width))
   {
      if ((sides & arm._side) == 0)
      {
         continue;
      }
      builder.box(arm._x0, y, arm._z0, arm._x1, top, arm._z1, asphalt);
      // one dash in the arm, along its length
      const auto along_x = arm._side == city::SidePositiveX || arm._side == city::SideNegativeX;
      const auto middle = (arm._side == city::SidePositiveX || arm._side == city::SidePositiveZ) ? 0.4f : -0.4f;
      if (along_x)
      {
         builder.box(middle - 0.06f, top, -0.015f, middle + 0.06f, top + 0.004f, 0.015f, marking);
      }
      else
      {
         builder.box(-0.015f, top, middle - 0.06f, 0.015f, top + 0.004f, middle + 0.06f, marking);
      }
   }
   // a straight road gets a dash in the middle too
   const auto straight_z = sides == (city::SidePositiveZ | city::SideNegativeZ);
   const auto straight_x = sides == (city::SidePositiveX | city::SideNegativeX);
   if (straight_z)
   {
      builder.box(-0.015f, top, -0.06f, 0.015f, top + 0.004f, 0.06f, marking);
   }
   if (straight_x)
   {
      builder.box(-0.06f, top, -0.015f, 0.06f, top + 0.004f, 0.015f, marking);
   }
}

/// \brief water from the center to every canal side, between low stone walls
void canals(Builder& builder, uint8_t sides)
{
   constexpr auto half_width = 0.28f;
   constexpr auto wall = 0.05f;
   constexpr auto wall_top = 0.05f;
   builder.box(-half_width, 0.0f, -half_width, half_width, 0.004f, half_width, water);
   for (const auto& arm : arms(half_width))
   {
      if ((sides & arm._side) != 0)
      {
         builder.box(arm._x0, 0.0f, arm._z0, arm._x1, 0.004f, arm._z1, water);
         // walls along both long edges of the arm
         if (arm._side == city::SidePositiveX || arm._side == city::SideNegativeX)
         {
            builder.box(arm._x0, 0.0f, half_width, arm._x1, wall_top, half_width + wall, stone);
            builder.box(arm._x0, 0.0f, -half_width - wall, arm._x1, wall_top, -half_width, stone);
         }
         else
         {
            builder.box(half_width, 0.0f, arm._z0, half_width + wall, wall_top, arm._z1, stone);
            builder.box(-half_width - wall, 0.0f, arm._z0, -half_width, wall_top, arm._z1, stone);
         }
         continue;
      }
      // a closed side of the center basin
      switch (arm._side)
      {
         case city::SidePositiveX:
            builder.box(half_width, 0.0f, -half_width - wall, half_width + wall, wall_top, half_width + wall, stone);
            break;
         case city::SideNegativeX:
            builder.box(-half_width - wall, 0.0f, -half_width - wall, -half_width, wall_top, half_width + wall, stone);
            break;
         case city::SidePositiveZ:
            builder.box(-half_width - wall, 0.0f, half_width, half_width + wall, wall_top, half_width + wall, stone);
            break;
         case city::SideNegativeZ:
            builder.box(-half_width - wall, 0.0f, -half_width - wall, half_width + wall, wall_top, -half_width, stone);
            break;
      }
   }
}

/// \brief a storey: facade with a band of windows
void storey(Builder& builder, float band_bottom, float band_top)
{
   constexpr auto w = building_half_width;
   builder.box(-w, 0.0f, -w, w, storey_height, w, facade, 1.0f);
   builder.box(-w - 0.006f, band_bottom * storey_height, -w - 0.006f, w + 0.006f, band_top * storey_height, w + 0.006f, glass);
}

void pillars(Builder& builder, float x, float z0, float z1)
{
   for (const auto z : {z0, z1})
   {
      builder.box(x - 0.05f, 0.0f, z - 0.05f, x + 0.05f, storey_height, z + 0.05f, concrete);
   }
}

void railings(Builder& builder, float y)
{
   for (const auto x : {-0.33f, 0.3f})
   {
      builder.box(x, y, -0.5f, x + 0.03f, y + 0.08f, 0.5f, railing);
   }
}

}  // namespace

bool isSymmetric(const city::TileInfo& tile)
{
   using city::Kind;
   return (tile._kind == Kind::Park && !tile._pillar) || tile._kind == Kind::Building || tile._kind == Kind::Floor ||
          tile._kind == Kind::FlatRoof || tile._kind == Kind::GardenRoof;
}

std::vector<Vertex> tileMesh(const city::TileInfo& tile)
{
   using city::Kind;
   Builder builder;
   switch (tile._kind)
   {
      case Kind::Air:
      case Kind::RampTop:  // the ramp tile below draws the whole slope
         break;

      case Kind::Road:
         slab(builder, sidewalk);
         roads(builder, tile._road_sides, 0.0f);
         if (tile._pillar)
         {
            // the deck above runs across the road, its pillars stand on the sidewalks
            builder.turn(-tile._quarter_turns);
            pillars(builder, 0.0f, -0.4f, 0.4f);
            builder.turn(tile._quarter_turns);
         }
         break;

      case Kind::Canal:
         slab(builder, stone);
         canals(builder, tile._canal_sides);
         break;

      case Kind::Bridge:
         slab(builder, stone);
         canals(builder, tile._canal_sides);
         // the road crosses on a raised deck
         builder.turn(-tile._quarter_turns);
         builder.box(-road_half_width - 0.04f, 0.0f, -0.5f, road_half_width + 0.04f, 0.07f, 0.5f, concrete);
         builder.box(-road_half_width, 0.07f, -0.5f, road_half_width, 0.082f, 0.5f, asphalt);
         builder.box(-0.015f, 0.082f, -0.1f, 0.015f, 0.086f, 0.1f, marking);
         railings(builder, 0.07f);
         builder.turn(tile._quarter_turns);
         break;

      case Kind::Park:
         slab(builder, grass);
         if (tile._pillar)
         {
            builder.turn(-tile._quarter_turns);
            pillars(builder, 0.0f, -0.25f, 0.25f);
            builder.tree(0.3f, 0.2f, 0.0f, 0.8f);
            builder.tree(-0.32f, -0.25f, 0.0f, 0.7f);
            builder.turn(tile._quarter_turns);
         }
         else
         {
            builder.tree(-0.22f, -0.18f);
            builder.tree(0.2f, 0.12f, 0.0f, 1.2f);
            builder.tree(-0.15f, 0.28f, 0.0f, 0.8f);
            builder.box(0.05f, 0.0f, -0.32f, 0.3f, 0.04f, -0.26f, door);  // a bench
         }
         break;

      case Kind::Building:
         slab(builder, sidewalk);
         storey(builder, 0.62f, 0.86f);
         builder.box(-0.08f, 0.0f, building_half_width, 0.08f, 0.3f * storey_height * 2.0f, building_half_width + 0.012f, door);
         builder.box(
            -0.12f, 0.62f * storey_height * 0.95f, building_half_width, 0.12f, 0.62f * storey_height, building_half_width + 0.08f, concrete
         );
         break;

      case Kind::Floor:
         storey(builder, 0.3f, 0.75f);
         break;

      case Kind::FlatRoof:
         builder.box(-0.45f, 0.0f, -0.45f, 0.45f, 0.05f, 0.45f, roof_slab);
         builder.box(0.08f, 0.05f, 0.05f, 0.28f, 0.16f, 0.25f, concrete);
         break;

      case Kind::GardenRoof:
         builder.box(-0.45f, 0.0f, -0.45f, 0.45f, 0.05f, 0.45f, roof_slab);
         builder.box(-0.4f, 0.05f, -0.4f, 0.4f, 0.08f, 0.4f, grass);
         builder.tree(-0.15f, 0.1f, 0.08f, 0.8f);
         builder.tree(0.2f, -0.15f, 0.08f, 0.6f);
         break;

      case Kind::PitchedRoof:
      {
         // ridge along z, gables at both ends
         constexpr auto w = 0.47f;
         constexpr auto ridge = 0.32f;
         builder.quad({-w, 0.0f, -w}, {-w, 0.0f, w}, {0.0f, ridge, w}, {0.0f, ridge, -w}, roof_tiles);
         builder.quad({w, 0.0f, -w}, {0.0f, ridge, -w}, {0.0f, ridge, w}, {w, 0.0f, w}, roof_tiles);
         builder.triangle(
            {-building_half_width, 0.0f, w - 0.03f}, {building_half_width, 0.0f, w - 0.03f}, {0.0f, ridge - 0.03f, w - 0.03f}, facade, 1.0f
         );
         builder.triangle(
            {-building_half_width, 0.0f, -w + 0.03f},
            {0.0f, ridge - 0.03f, -w + 0.03f},
            {building_half_width, 0.0f, -w + 0.03f},
            facade,
            1.0f
         );
         builder.turn(tile._quarter_turns);
         break;
      }

      case Kind::Ramp:
      {
         // built facing -z (the street) and rising towards +z, where the deck starts one storey up
         slab(builder, sidewalk);
         constexpr auto h = storey_height;
         constexpr auto w = road_half_width;
         builder.quad({-w, 0.012f, -0.5f}, {-w, h, 0.5f}, {w, h, 0.5f}, {w, 0.012f, -0.5f}, asphalt);
         builder.quad({-w, h, 0.5f}, {-w, 0.0f, 0.5f}, {w, 0.0f, 0.5f}, {w, h, 0.5f}, concrete);
         for (const auto x : {-w, w})
         {
            builder.triangle({x, 0.0f, -0.5f}, {x, h, 0.5f}, {x, 0.0f, 0.5f}, concrete);
            builder.quad({x, 0.012f, -0.5f}, {x, h + 0.08f, 0.5f}, {x + 0.03f, h + 0.08f, 0.5f}, {x + 0.03f, 0.012f, -0.5f}, railing);
         }
         builder.box(-0.015f, 0.25f * h + 0.004f, -0.18f, 0.015f, 0.25f * h + 0.012f, -0.06f, marking);
         builder.turn(tile._quarter_turns);
         break;
      }

      case Kind::Deck:
         // along z, at the bottom of its layer, which is the top of the pillars below
         builder.box(-road_half_width - 0.04f, -0.06f, -0.5f, road_half_width + 0.04f, 0.0f, 0.5f, concrete);
         builder.box(-road_half_width, 0.0f, -0.5f, road_half_width, 0.012f, 0.5f, asphalt);
         builder.box(-0.015f, 0.012f, -0.1f, 0.015f, 0.016f, 0.1f, marking);
         railings(builder, 0.0f);
         builder.turn(tile._quarter_turns);
         break;
   }
   return builder.take();
}

std::vector<Vertex> groundMesh(float half_width, float half_depth)
{
   Builder builder;
   constexpr Color field{0.62f, 0.66f, 0.56f};
   constexpr Color lot{0.47f, 0.47f, 0.44f};
   builder.quad({-60.0f, -0.07f, -60.0f}, {-60.0f, -0.07f, 60.0f}, {60.0f, -0.07f, 60.0f}, {60.0f, -0.07f, -60.0f}, field);
   builder.box(-half_width - 0.15f, -0.075f, -half_depth - 0.15f, half_width + 0.15f, -0.061f, half_depth + 0.15f, lot);
   return builder.take();
}

}  // namespace testbed
