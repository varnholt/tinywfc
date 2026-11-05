// headless benchmark: solves each scenario with several seeds and prints the median time and how often attempts failed.
// usage: tinywfc_bench [--runs N] [--threads N]   (threads: parallel attempts in run(); 0 = one per hardware thread)

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "scenarios.h"

namespace
{

int32_t parseInt(std::string_view text, int32_t fallback)
{
   auto value = fallback;
   std::from_chars(text.data(), text.data() + text.size(), value);
   return value;
}

}  // namespace

int main(int argument_count, char** arguments)
{
   auto runs = 7;
   auto threads = 1;
   const std::span arguments_span{arguments, static_cast<std::size_t>(argument_count)};
   for (std::size_t index = 1; index + 1 < arguments_span.size(); ++index)
   {
      const std::string_view argument{arguments_span[index]};
      if (argument == "--runs")
      {
         runs = std::max(1, parseInt(arguments_span[index + 1], runs));
      }
      if (argument == "--threads")
      {
         threads = std::max(0, parseInt(arguments_span[index + 1], threads));
      }
   }

   std::println("tinywfc {}.{}.{}, {} runs per scenario, threads {} ({} hardware)", tinywfc::version_major, tinywfc::version_minor,
                tinywfc::version_patch, runs, threads, std::thread::hardware_concurrency());
   std::println("{:<20} {:>10} {:>10} {:>9} {:>11} {:>9}", "scenario", "median ms", "max ms", "solved", "backtracks", "restarts");

   for (const auto& scenario : bench::scenarios)
   {
      const auto rules = scenario._rules();
      std::vector<double> times;
      auto solved = 0;
      auto backtracks = 0;
      auto restarts = 0;
      for (auto run = 0; run < runs; ++run)
      {
         const auto start = std::chrono::steady_clock::now();
         auto solver = tinywfc::Solver::create(
            rules, {._size = scenario._size, ._seed = static_cast<uint64_t>(run), ._max_restarts = 20, ._threads = threads}
         );
         const auto result = solver->run();
         times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
         solved += result ? 1 : 0;
         backtracks += solver->statistics()._backtracks;
         restarts += solver->statistics()._restarts;
      }
      std::ranges::sort(times);
      std::println(
         "{:<20} {:>10.2f} {:>10.2f} {:>6}/{:<2} {:>11.1f} {:>9.2f}",
         scenario._name,
         times[times.size() / 2],
         times.back(),
         solved,
         runs,
         static_cast<double>(backtracks) / runs,
         static_cast<double>(restarts) / runs
      );
   }
   return 0;
}
