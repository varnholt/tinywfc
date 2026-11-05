// the testbed's city tileset: it must always produce a valid city, so the live demo never gets stuck

#include <algorithm>
#include <cstdint>

#include "city.h"
#include "test_framework.h"

TEST_CASE("city: every seed produces a valid city")
{
   const auto& set = city::tileset();
   auto total_backtracks = 0;
   for (uint64_t seed = 0; seed < 12; ++seed)
   {
      const tinywfc::Size size{14, 5, 14};
      auto solver = city::makeSolver(size, seed);
      CHECK(solver.has_value());
      const auto result = solver->run();
      CHECK_MESSAGE(result.has_value(), "seed {}", seed);
      if (!result)
      {
         continue;
      }
      total_backtracks += solver->statistics()._backtracks;

      for (auto z = 0; z < size.z; ++z)
      {
         for (auto x = 0; x < size.x; ++x)
         {
            // ground tiles on the ground, upper tiles above, never an open floor at the top
            for (auto y = 0; y < size.y; ++y)
            {
               const auto tile = *solver->tile({x, y, z});
               const auto& list = y == 0 ? set._ground_tiles : set._upper_tiles;
               CHECK(std::ranges::find(list, tile) != list.end());
               if (y == size.y - 1)
               {
                  CHECK(std::ranges::find(set._floor_tiles, tile) == set._floor_tiles.end());
               }
            }

            // a building column is building, floors, one roof, then air
            const auto& ground = set._tiles[*solver->tile({x, 0, z})];
            auto roof_seen = ground._kind != city::Kind::Building;
            for (auto y = 1; y < size.y; ++y)
            {
               const auto kind = set._tiles[*solver->tile({x, y, z})]._kind;
               if (roof_seen)
               {
                  CHECK_MESSAGE(kind == city::Kind::Air, "seed {} column {} {} layer {}", seed, x, z, y);
               }
               else if (kind != city::Kind::Floor)
               {
                  CHECK(kind == city::Kind::FlatRoof || kind == city::Kind::GardenRoof || kind == city::Kind::PitchedRoof);
                  roof_seen = true;
               }
            }
            CHECK(roof_seen);
         }
      }
   }
   std::println("   {} backtracks over 12 cities", total_backtracks);
}

TEST_CASE("city: roads connect")
{
   // a road side always meets a road side (or the border), so every road tile continues into a neighbor
   const auto& set = city::tileset();
   auto solver = city::makeSolver({16, 3, 16}, 3);
   CHECK(solver->run().has_value());
   for (auto z = 0; z < 16; ++z)
   {
      for (auto x = 0; x < 16; ++x)
      {
         const auto& tile = set._tiles[*solver->tile({x, 0, z})];
         if (x + 1 < 16)
         {
            const auto& right = set._tiles[*solver->tile({x + 1, 0, z})];
            const auto here = (tile._road_sides & city::SidePositiveX) != 0;
            const auto there = (right._road_sides & city::SideNegativeX) != 0;
            CHECK(here == there);
         }
         if (z + 1 < 16)
         {
            const auto& front = set._tiles[*solver->tile({x, 0, z + 1})];
            const auto here = (tile._road_sides & city::SidePositiveZ) != 0;
            const auto there = (front._road_sides & city::SideNegativeZ) != 0;
            CHECK(here == there);
         }
      }
   }
}

TEST_CASE("city: too few layers are rejected")
{
   CHECK(city::makeSolver({4, 1, 4}, 0).error() == tinywfc::Error::InvalidSize);
}

int main()
{
   return test::runAll();
}
