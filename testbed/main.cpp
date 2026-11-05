// tinywfc city testbed: wave function collapse assembling a small 3d city, drawn with opengl es 3.0 / webgl2.
//
//   tinywfc_testbed [--seed N] [--grid XxYxZ] [--size WxH] [--speed CELLS_PER_SECOND]
//                   [--screenshot out.png [--ui]]
//                   [--record DIR [--frames N] [--fps F] [--orbit RADIANS_PER_SECOND]]
//
// --screenshot and --record render offscreen in a hidden window and quit; --record writes DIR/frame_0000.png ...,
// with the playback paced so the city is complete after two thirds of the frames.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "app.h"
#include "gl.h"

namespace
{

using testbed::App;

struct Options
{
   uint64_t _seed{1};
   tinywfc::Size _grid{16, 6, 16};
   int32_t _width{1280};
   int32_t _height{720};
   float _speed{240.0f};
   std::optional<std::string> _screenshot;
   bool _ui{false};
   std::optional<std::string> _record;
   int32_t _frames{150};
   float _fps{30.0f};
   float _orbit{0.25f};
};

template <class T>
bool parse(std::string_view text, T& value)
{
   const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
   return error == std::errc{} && end == text.data() + text.size();
}

std::optional<Options> parseOptions(std::span<char*> arguments)
{
   Options options;
   for (std::size_t index = 1; index < arguments.size(); ++index)
   {
      const std::string_view argument = arguments[index];
      const std::string_view value = index + 1 < arguments.size() ? std::string_view{arguments[index + 1]} : std::string_view{};
      auto ok = true;
      if (argument == "--ui")
      {
         options._ui = true;
         continue;
      }
      if (argument == "--seed")
      {
         ok = parse(value, options._seed);
      }
      else if (argument == "--speed")
      {
         ok = parse(value, options._speed);
      }
      else if (argument == "--frames")
      {
         ok = parse(value, options._frames) && options._frames > 0;
      }
      else if (argument == "--fps")
      {
         ok = parse(value, options._fps) && options._fps > 0.0f;
      }
      else if (argument == "--orbit")
      {
         ok = parse(value, options._orbit);
      }
      else if (argument == "--screenshot" && !value.empty())
      {
         options._screenshot = std::string{value};
      }
      else if (argument == "--record" && !value.empty())
      {
         options._record = std::string{value};
      }
      else if (argument == "--size")
      {
         const auto x = value.find('x');
         ok = x != std::string_view::npos && parse(value.substr(0, x), options._width) && parse(value.substr(x + 1), options._height);
      }
      else if (argument == "--grid")
      {
         const auto first = value.find('x');
         const auto second = value.find('x', first + 1);
         ok = first != std::string_view::npos && second != std::string_view::npos && parse(value.substr(0, first), options._grid.x) &&
              parse(value.substr(first + 1, second - first - 1), options._grid.y) && parse(value.substr(second + 1), options._grid.z) &&
              options._grid.y >= 2;
      }
      else
      {
         ok = false;
      }
      if (!ok)
      {
         std::println(stderr, "unknown or invalid option {} {}", argument, value);
         return std::nullopt;
      }
      ++index;
   }
   return options;
}

/// \brief sdl objects in teardown order
struct Window
{
   SDL_Window* _window{nullptr};
   SDL_GLContext _context{nullptr};
   bool _imgui{false};

   Window() = default;
   Window(const Window&) = delete;
   Window& operator=(const Window&) = delete;
   ~Window()
   {
      if (_imgui)
      {
         ImGui_ImplOpenGL3_Shutdown();
         ImGui_ImplSDL3_Shutdown();
         ImGui::DestroyContext();
      }
      if (_context != nullptr)
      {
         SDL_GL_DestroyContext(_context);
      }
      if (_window != nullptr)
      {
         SDL_DestroyWindow(_window);
      }
      SDL_Quit();
   }
};

/// \brief an opengl es 3.0 context. natively the driver's es profile is used where it exists; otherwise a 4.3 core
///        context, which accepts the same "#version 300 es" shaders (ARB_ES3_compatibility) and is only used through the
///        es 3.0 subset that glad loads.
bool createContext(Window& window, int32_t width, int32_t height, bool hidden)
{
   const auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (hidden ? SDL_WINDOW_HIDDEN : 0);
   const auto attempt = [&](int32_t profile, int32_t major, int32_t minor)
   {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
      SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
      SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
      SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
      window._window = SDL_CreateWindow("tinywfc city", width, height, flags);
      if (window._window == nullptr)
      {
         return false;
      }
      window._context = SDL_GL_CreateContext(window._window);
      if (window._context != nullptr)
      {
         return true;
      }
      SDL_DestroyWindow(window._window);
      window._window = nullptr;
      return false;
   };
   if (attempt(SDL_GL_CONTEXT_PROFILE_ES, 3, 0))
   {
      return true;
   }
#if !defined(__EMSCRIPTEN__)
   std::println(stderr, "no opengl es 3.0 context ({}), trying opengl 4.3 core", SDL_GetError());
   return attempt(SDL_GL_CONTEXT_PROFILE_CORE, 4, 3);
#else
   return false;
#endif
}

void initImgui(Window& window)
{
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::GetIO().IniFilename = nullptr;
   ImGui::StyleColorsDark();
   const auto scale = SDL_GetWindowDisplayScale(window._window);
   ImGui::GetStyle().ScaleAllSizes(scale);
   ImGui::GetStyle().FontScaleDpi = scale;
   ImGui_ImplSDL3_InitForOpenGL(window._window, window._context);
   ImGui_ImplOpenGL3_Init("#version 300 es");
   window._imgui = true;
}

void buildUi(App& app)
{
   ImGui_ImplOpenGL3_NewFrame();
   ImGui_ImplSDL3_NewFrame();
   ImGui::NewFrame();
   testbed::drawUi(app);
   ImGui::Render();
}

struct Session
{
   Window _window;
   App _app;
   uint64_t _last_counter{0};
   bool _running{true};
};

void handleEvent(Session& session, const SDL_Event& event)
{
   ImGui_ImplSDL3_ProcessEvent(&event);
   auto& app = session._app;
   const auto& io = ImGui::GetIO();
   switch (event.type)
   {
      case SDL_EVENT_QUIT:
         session._running = false;
         break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
         app._orbiting = !io.WantCaptureMouse && event.button.button == SDL_BUTTON_LEFT;
         break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
         app._orbiting = false;
         break;
      case SDL_EVENT_MOUSE_MOTION:
         if (app._orbiting)
         {
            app._camera._yaw += event.motion.xrel * 0.008f;
            app._camera._pitch = std::clamp(app._camera._pitch + event.motion.yrel * 0.008f, 0.1f, 1.45f);
         }
         break;
      case SDL_EVENT_MOUSE_WHEEL:
         if (!io.WantCaptureMouse)
         {
            app._camera._distance = std::clamp(app._camera._distance * (event.wheel.y > 0 ? 0.9f : 1.1f), 3.0f, 120.0f);
         }
         break;
      case SDL_EVENT_KEY_DOWN:
         if (!io.WantCaptureKeyboard && event.key.key == SDLK_ESCAPE)
         {
            session._running = false;
         }
         break;
      default:
         break;
   }
}

void frame(Session& session)
{
   SDL_Event event;
   while (SDL_PollEvent(&event))
   {
      handleEvent(session, event);
   }
   const auto counter = SDL_GetPerformanceCounter();
   const auto dt =
      session._last_counter == 0
         ? 0.0f
         : static_cast<float>(static_cast<double>(counter - session._last_counter) / static_cast<double>(SDL_GetPerformanceFrequency()));
   session._last_counter = counter;

   auto& app = session._app;
   app.update(std::min(dt, 0.1f));
   buildUi(app);
   auto width = 0;
   auto height = 0;
   SDL_GetWindowSizeInPixels(session._window._window, &width, &height);
   app._renderer->draw(width, height, app._camera, app.instances());
   ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
   SDL_GL_SwapWindow(session._window._window);
}

/// \brief --screenshot and --record: fixed time steps, offscreen, no window on screen
int render(Session& session, const Options& options)
{
   auto& app = session._app;
   // solve completely first, so the pacing is known
   while (app._generator.status()._running)
   {
      SDL_Delay(1);
   }
   app._auto_rotate = false;

   if (options._screenshot)
   {
      app.showAll();
      std::function<void()> overlay;
      if (options._ui)
      {
         initImgui(session._window);
         for (auto settle = 0; settle < 3; ++settle)
         {
            buildUi(app);
         }
         overlay = [] { ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); };
      }
      return app._renderer->screenshot(*options._screenshot, options._width, options._height, app._camera, app.instances(), overlay) ? 0
                                                                                                                                     : 1;
   }

   const auto directory = std::filesystem::path{*options._record};
   std::filesystem::create_directories(directory);
   const auto dt = 1.0f / options._fps;
   // the number of collapse events is about the number of cells; finish after two thirds of the clip
   app._cells_per_second = static_cast<float>(app._cells.size()) / (static_cast<float>(options._frames) * dt * 0.62f);
   for (auto index = 0; index < options._frames; ++index)
   {
      app.update(dt);
      app._camera._yaw += options._orbit * dt;
      const auto file = directory / std::format("frame_{:04}.png", index);
      if (!app._renderer->screenshot(file, options._width, options._height, app._camera, app.instances()))
      {
         std::println(stderr, "could not write {}", file.string());
         return 1;
      }
   }
   return 0;
}

}  // namespace

int main(int argument_count, char** arguments)
{
   const auto options = parseOptions(std::span(arguments, static_cast<std::size_t>(argument_count)));
   if (!options)
   {
      return 2;
   }
   if (!SDL_Init(SDL_INIT_VIDEO))
   {
      std::println(stderr, "SDL_Init failed: {}", SDL_GetError());
      return 1;
   }

   auto session = std::make_unique<Session>();
   const auto offscreen = options->_screenshot.has_value() || options->_record.has_value();
   if (!createContext(session->_window, options->_width, options->_height, offscreen))
   {
      std::println(stderr, "could not create an opengl es 3.0 context: {}", SDL_GetError());
      return 1;
   }
   SDL_GL_MakeCurrent(session->_window._window, session->_window._context);
#if !defined(__EMSCRIPTEN__)
   if (gladLoadGLES2(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) == 0)
   {
      std::println(stderr, "could not load the opengl es 3.0 functions");
      return 1;
   }
#endif
   SDL_GL_SetSwapInterval(1);

   auto& app = session->_app;
   app._seed = options->_seed;
   app._size = options->_grid;
   app._cells_per_second = options->_speed;
   try
   {
      app._renderer.emplace();
   }
   catch (const std::exception& error)
   {
      std::println(stderr, "{}", error.what());
      return 1;
   }
   app.newCity();
   app.frameCamera();

   if (offscreen)
   {
      return render(*session, *options);
   }

   initImgui(session->_window);
#if defined(__EMSCRIPTEN__)
   // the browser owns the loop; the session lives on after main returns
   emscripten_set_main_loop_arg([](void* user) { frame(*static_cast<Session*>(user)); }, session.release(), 0, false);
#else
   while (session->_running)
   {
      frame(*session);
   }
#endif
   return 0;
}
