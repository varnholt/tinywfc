#include "generator.h"

#include "city.h"

namespace testbed
{

namespace
{

/// \brief solver steps per frame without threads; enough for a city in a few seconds, little enough for 60 fps
constexpr auto steps_per_poll = 24;

}  // namespace

Generator::~Generator() = default;

void Generator::push(const Event& event)
{
   const std::scoped_lock lock{_mutex};
   _events.push_back(event);
}

void Generator::attach(tinywfc::Solver& solver)
{
   solver.setCollapseCallback([this, &solver](const tinywfc::Coordinate& cell, tinywfc::TileId tile)
                              { push({Event::Type::Collapse, static_cast<uint32_t>(solver.cellIndex(cell)), tile}); });
   // after a backtrack or restart the viewer starts over; every cell that is still decided is sent again
   const auto resend = [this, &solver]()
   {
      push({Event::Type::Reset});
      for (std::size_t cell = 0; cell < solver.cellCount(); ++cell)
      {
         if (const auto tile = solver.tile(solver.coordinate(cell)))
         {
            push({Event::Type::Collapse, static_cast<uint32_t>(cell), *tile});
         }
      }
   };
   solver.setResetCallback(resend);
   resend();
}

bool Generator::advance(tinywfc::Solver& solver)
{
   const auto result = solver.step();
   auto done = false;
   auto failed = false;
   if (!result)
   {
      // backtracking gave up: next attempt, as run() would do
      if (solver.statistics()._restarts < solver.settings()._max_restarts)
      {
         solver.restart();
      }
      else
      {
         failed = true;
      }
   }
   else
   {
      done = !*result;
   }

   const std::scoped_lock lock{_mutex};
   _status._statistics = solver.statistics();
   _status._collapsed = solver.collapsedCount();
   if (done || failed)
   {
      _status._running = false;
      _status._finished = done;
      _status._failed = failed;
      _status._solve_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _start_time).count();
      _events.push_back({done ? Event::Type::Finished : Event::Type::Failed});
      return false;
   }
   return true;
}

void Generator::start(const tinywfc::Size& size, uint64_t seed)
{
#if TINYWFC_TESTBED_THREADS
   // stop and join the previous worker before its queue is cleared
   _worker = {};
#endif
   {
      const std::scoped_lock lock{_mutex};
      _events.clear();
      _status = {._running = true, ._cells = static_cast<std::size_t>(size.cellCount())};
      _start_time = std::chrono::steady_clock::now();
   }

#if TINYWFC_TESTBED_THREADS
   _worker = tinywfc::detail::JoiningThread{[this, size, seed](tinywfc::detail::StopToken stop)
                                            {
                                               auto solver = city::makeSolver(size, seed);
                                               if (!solver)
                                               {
                                                  push({Event::Type::Failed});
                                                  return;
                                               }
                                               attach(*solver);
                                               while (!stop.stop_requested() && advance(*solver))
                                               {
                                               }
                                            }};
#else
   _solver.reset();
   auto solver = city::makeSolver(size, seed);
   if (!solver)
   {
      push({Event::Type::Failed});
      return;
   }
   _solver.emplace(std::move(*solver));
   attach(*_solver);
#endif
}

void Generator::poll(std::vector<Event>& out, std::size_t max_collapses)
{
#if !TINYWFC_TESTBED_THREADS
   if (_solver)
   {
      for (auto step = 0; step < steps_per_poll; ++step)
      {
         if (!advance(*_solver))
         {
            _solver.reset();
            break;
         }
      }
   }
#endif
   const std::scoped_lock lock{_mutex};
   auto collapses = std::size_t{0};
   while (!_events.empty() && collapses < max_collapses)
   {
      const auto& event = _events.front();
      collapses += event._type == Event::Type::Collapse ? 1 : 0;
      out.push_back(event);
      _events.pop_front();
   }
}

GeneratorStatus Generator::status() const
{
   const std::scoped_lock lock{_mutex};
   return _status;
}

}  // namespace testbed
