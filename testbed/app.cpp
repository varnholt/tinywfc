#include "app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "city.h"

namespace testbed
{

namespace
{

constexpr auto drop_duration = 0.45f;
constexpr auto drop_height = 1.6f;

/// \brief facade colors; each building column picks one from its position and the seed
constexpr std::array<std::array<float, 3>, 7> facades{{
   {0.93f, 0.87f, 0.76f},
   {0.85f, 0.62f, 0.50f},
   {0.74f, 0.80f, 0.86f},
   {0.95f, 0.93f, 0.90f},
   {0.80f, 0.73f, 0.86f},
   {0.72f, 0.82f, 0.70f},
   {0.96f, 0.80f, 0.52f},
}};

uint32_t hash(uint32_t a, uint32_t b, uint32_t c)
{
   auto h = a * 0x9e3779b1u ^ b * 0x85ebca77u ^ c * 0xc2b2ae3du;
   h ^= h >> 15;
   h *= 0x2c1b3c6du;
   h ^= h >> 12;
   return h;
}

}  // namespace

void App::newCity()
{
   _cells.assign(static_cast<std::size_t>(_size.cellCount()), {});
   _shown_count = 0;
   _budget = 0.0f;
   _events.clear();
   if (_renderer)
   {
      _renderer->setCitySize(static_cast<float>(_size.x), static_cast<float>(_size.z));
   }
   _generator.start(_size, _seed);
   _status = _generator.status();
}

void App::frameCamera()
{
   _camera._target = {0.0f, 0.6f, 0.0f};
   _camera._distance = static_cast<float>(std::max(_size.x, _size.z)) * 1.12f + 3.0f;
}

void App::apply(const Event& event)
{
   switch (event._type)
   {
      case Event::Type::Collapse:
      {
         auto& cell = _cells[event._cell];
         if (cell._tile < 0)
         {
            ++_shown_count;
         }
         cell._tile = static_cast<int32_t>(event._tile);
         if (cell._last_tile != cell._tile)
         {
            cell._appear_time = _time;
            cell._last_tile = cell._tile;
         }
         break;
      }
      case Event::Type::Reset:
         for (auto& cell : _cells)
         {
            cell._tile = -1;
         }
         _shown_count = 0;
         break;
      case Event::Type::Finished:
      case Event::Type::Failed:
         break;
   }
}

void App::update(float dt)
{
   _time += dt;
   _status = _generator.status();
   if (_auto_rotate)
   {
      _camera._yaw += dt * 0.08f;
   }
   if (_paused)
   {
      return;
   }

   std::size_t allowed = 0;
   if (_instant)
   {
      allowed = _cells.size() * 4;
   }
   else
   {
      _budget += dt * _cells_per_second;
      allowed = static_cast<std::size_t>(_budget);
      _budget -= static_cast<float>(allowed);
   }
   _events.clear();
   _generator.poll(_events, allowed);
   for (const auto& event : _events)
   {
      apply(event);
   }
}

void App::showAll()
{
   for (auto round = 0; round < 1000; ++round)
   {
      _events.clear();
      _generator.poll(_events, _cells.size() * 8);
      for (const auto& event : _events)
      {
         apply(event);
      }
      _status = _generator.status();
      if (_events.empty() && !_status._running)
      {
         break;
      }
   }
   for (auto& cell : _cells)
   {
      cell._appear_time = -100.0f;
   }
}

std::vector<Instance> App::instances() const
{
   const auto& set = city::tileset();
   std::vector<Instance> result;
   result.reserve(_cells.size());
   const auto layer = static_cast<std::size_t>(_size.x) * static_cast<std::size_t>(_size.z);
   for (std::size_t index = 0; index < _cells.size(); ++index)
   {
      const auto& cell = _cells[index];
      if (cell._tile < 0)
      {
         continue;
      }
      const auto& tile = set._tiles[static_cast<std::size_t>(cell._tile)];
      if (tile._kind == city::Kind::Air)
      {
         continue;
      }
      const auto x = static_cast<int32_t>(index % static_cast<std::size_t>(_size.x));
      const auto y = static_cast<int32_t>(index / layer);
      const auto z = static_cast<int32_t>((index % layer) / static_cast<std::size_t>(_size.x));

      // tiles drop into place when they are decided
      const auto age = (_time - cell._appear_time) / drop_duration;
      const auto fall = age < 1.0f ? (1.0f - age) * (1.0f - age) * drop_height : 0.0f;

      Instance instance;
      instance._tile = static_cast<uint32_t>(cell._tile);
      instance._position = {
         static_cast<float>(x) - static_cast<float>(_size.x) * 0.5f + 0.5f,
         static_cast<float>(y) * storey_height + fall,
         static_cast<float>(z) - static_cast<float>(_size.z) * 0.5f + 0.5f,
      };
      const auto column = hash(static_cast<uint32_t>(x), static_cast<uint32_t>(z), static_cast<uint32_t>(_seed));
      instance._tint = facades[column % facades.size()];
      if (isSymmetric(tile))
      {
         instance._turn = static_cast<float>(column / 7 % 4) * std::numbers::pi_v<float> * 0.5f;
      }
      result.push_back(instance);
   }
   return result;
}

}  // namespace testbed
