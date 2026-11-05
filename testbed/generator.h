#pragma once

// runs the solver away from the render loop. natively a worker thread (std::jthread) solves the city and queues every collapse; the
// render thread plays the queue back at its own pace, so the ui never waits. a browser without threads gets the same
// interface, with the solver advanced by a bounded number of steps per frame inside poll().

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include <tinywfc/tinywfc.h>

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define TINYWFC_TESTBED_THREADS 0
#else
#define TINYWFC_TESTBED_THREADS 1
#endif

namespace testbed
{

struct Event
{
   enum class Type : uint8_t
   {
      Collapse,  //!< _cell now shows _tile
      Reset,     //!< backtrack or restart: forget every shown tile, the collapses that are still valid follow
      Finished,
      Failed,
   };

   Type _type{Type::Collapse};
   uint32_t _cell{0};
   tinywfc::TileId _tile{0};
};

struct GeneratorStatus
{
   bool _running{false};
   bool _finished{false};
   bool _failed{false};
   tinywfc::Statistics _statistics;
   std::size_t _collapsed{0};
   std::size_t _cells{0};
   double _solve_ms{0.0};
};

class Generator
{
public:
   Generator() = default;
   Generator(const Generator&) = delete;
   Generator& operator=(const Generator&) = delete;
   ~Generator();

   /// \brief cancels a running city and starts a new one
   void start(const tinywfc::Size& size, uint64_t seed);

   /// \brief moves queued events into out, at most max_collapses collapse events (resets and the end are free)
   void poll(std::vector<Event>& out, std::size_t max_collapses);

   [[nodiscard]] GeneratorStatus status() const;

   [[nodiscard]] static constexpr bool threaded()
   {
      return TINYWFC_TESTBED_THREADS != 0;
   }

private:
   /// \brief one solver step; false when the city is done or failed
   bool advance(tinywfc::Solver& solver);
   void push(const Event& event);
   void attach(tinywfc::Solver& solver);

   mutable std::mutex _mutex;
   std::deque<Event> _events;
   GeneratorStatus _status;
   std::chrono::steady_clock::time_point _start_time;

#if TINYWFC_TESTBED_THREADS
   tinywfc::detail::JoiningThread _worker;  // std::jthread where the standard library has it
#else
   std::optional<tinywfc::Solver> _solver;
#endif
};

}  // namespace testbed
