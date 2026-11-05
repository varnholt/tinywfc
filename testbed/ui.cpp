#include <imgui.h>

#include <algorithm>
#include <format>
#include <map>
#include <random>
#include <string>

#include "app.h"
#include "city.h"

namespace testbed
{

namespace
{

void text(const std::string& line)
{
   ImGui::TextUnformatted(line.c_str());
}

/// \brief the tiles of the set with how often they appear in the shown city; replaces the old tile grid view
void drawTiles(const App& app)
{
   if (!ImGui::CollapsingHeader("tiles"))
   {
      return;
   }
   const auto& set = city::tileset();
   std::map<std::string_view, int32_t> counts;
   std::map<std::string_view, float> weights;
   for (std::size_t tile = 0; tile < set._tiles.size(); ++tile)
   {
      weights[set._tiles[tile]._name] += set._rules.weight(static_cast<tinywfc::TileId>(tile));
   }
   for (const auto& cell : app._cells)
   {
      if (cell._tile >= 0)
      {
         ++counts[set._tiles[static_cast<std::size_t>(cell._tile)]._name];
      }
   }
   if (ImGui::BeginTable("tiles", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
   {
      ImGui::TableSetupColumn("tile");
      ImGui::TableSetupColumn("weight");
      ImGui::TableSetupColumn("in city");
      ImGui::TableHeadersRow();
      for (const auto& [name, weight] : weights)
      {
         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         text(std::string{name});
         ImGui::TableNextColumn();
         text(std::format("{:.2f}", weight));
         ImGui::TableNextColumn();
         text(std::format("{}", counts[name]));
      }
      ImGui::EndTable();
   }
}

}  // namespace

void drawUi(App& app)
{
   ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowBgAlpha(0.88f);
   ImGui::Begin("tinywfc city", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

   // city
   auto seed = static_cast<int32_t>(app._seed);
   if (ImGui::InputInt("seed", &seed))
   {
      app._seed = static_cast<uint64_t>(std::max(seed, 0));
      app.newCity();
   }
   if (ImGui::Button("new city"))
   {
      app._seed = std::random_device{}() % 100000u;
      app.newCity();
   }
   ImGui::SameLine();
   if (ImGui::Button("replay"))
   {
      app.newCity();
   }
   ImGui::SameLine();
   if (ImGui::Button("finish"))
   {
      app.showAll();
   }

   // a new city once a slider is released, not on every pixel of the drag
   auto size_changed = false;
   ImGui::SliderInt("width", &app._size.x, 4, 32);
   size_changed |= ImGui::IsItemDeactivatedAfterEdit();
   ImGui::SliderInt("depth", &app._size.z, 4, 32);
   size_changed |= ImGui::IsItemDeactivatedAfterEdit();
   ImGui::SliderInt("layers", &app._size.y, 2, 10);
   size_changed |= ImGui::IsItemDeactivatedAfterEdit();
   if (size_changed)
   {
      app.newCity();
      app.frameCamera();
   }

   // playback
   ImGui::SeparatorText("playback");
   ImGui::SliderFloat("cells / s", &app._cells_per_second, 20.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
   ImGui::Checkbox("instant", &app._instant);
   ImGui::SameLine();
   ImGui::Checkbox("pause", &app._paused);
   ImGui::SameLine();
   ImGui::Checkbox("rotate", &app._auto_rotate);

   // solver
   ImGui::SeparatorText("solver");
   const auto& status = app._status;
   const auto state = status._running ? "solving" : status._finished ? "done" : status._failed ? "failed" : "idle";
   text(std::format("{} {}x{}x{} = {} cells", state, app._size.x, app._size.y, app._size.z, status._cells));
   text(std::format("collapsed {} / {}, shown {}", status._collapsed, status._cells, app._shown_count));
   text(std::format(
      "decisions {}, backtracks {}, restarts {}",
      status._statistics._decisions,
      status._statistics._backtracks,
      status._statistics._restarts
   ));
   if (status._finished || status._failed)
   {
      text(std::format("solved in {:.1f} ms {}", status._solve_ms, Generator::threaded() ? "on a worker thread" : "between frames"));
   }

   drawTiles(app);

   ImGui::SeparatorText("view");
   text("drag: orbit, wheel: zoom");
   text(std::format("{:.0f} fps", ImGui::GetIO().Framerate));
   ImGui::End();
}

}  // namespace testbed
