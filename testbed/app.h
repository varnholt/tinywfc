#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "generator.h"
#include "renderer.h"

namespace testbed
{

struct CityView
{
   int32_t _tile{-1};       //!< shown tile, -1 = open
   int32_t _last_tile{-1};  //!< shown before the last reset, so a tile that comes back does not drop in again
   float _appear_time{-1.0f};
};

/// \brief everything the frame loop needs: the city being built, its playback and the camera.
struct App
{
   // city settings, applied by newCity()
   tinywfc::Size _size{16, 6, 16};
   uint64_t _seed{1};

   // playback
   float _cells_per_second{240.0f};
   bool _instant{false};
   bool _paused{false};
   bool _auto_rotate{true};
   float _time{0.0f};

   Generator _generator;
   GeneratorStatus _status;
   std::vector<CityView> _cells;
   std::vector<Event> _events;
   std::size_t _shown_count{0};
   float _budget{0.0f};

   Camera _camera;
   std::optional<Renderer> _renderer;

   // mouse
   bool _orbiting{false};

   /// \brief starts generating a city with the current settings
   void newCity();

   /// \brief advances the playback by dt seconds of wall time
   void update(float dt);

   /// \brief applies every queued event at once
   void showAll();

   /// \brief frames the camera on the city
   void frameCamera();

   [[nodiscard]] std::vector<Instance> instances() const;

private:
   void apply(const Event& event);
};

/// \brief the dear imgui panel
void drawUi(App& app);

}  // namespace testbed
