#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

#include "gl.h"
#include "math3d.h"
#include "meshes.h"

namespace testbed
{

/// \brief owns one gl object name and frees it with the matching glDelete* call
template <void (*Delete)(GLuint)>
class GlObject
{
public:
   GlObject() = default;
   explicit GlObject(GLuint name) : _name(name)
   {
   }
   GlObject(GlObject&& other) noexcept : _name(std::exchange(other._name, 0u))
   {
   }
   GlObject& operator=(GlObject&& other) noexcept
   {
      std::swap(_name, other._name);
      return *this;
   }
   GlObject(const GlObject&) = delete;
   GlObject& operator=(const GlObject&) = delete;
   ~GlObject()
   {
      if (_name != 0)
      {
         Delete(_name);
      }
   }

   [[nodiscard]] GLuint get() const noexcept
   {
      return _name;
   }

private:
   GLuint _name{0};
};

void deleteBuffer(GLuint name);
void deleteVertexArray(GLuint name);
void deleteProgram(GLuint name);
void deleteFramebuffer(GLuint name);
void deleteRenderbuffer(GLuint name);

using Buffer = GlObject<deleteBuffer>;
using VertexArray = GlObject<deleteVertexArray>;
using Program = GlObject<deleteProgram>;
using Framebuffer = GlObject<deleteFramebuffer>;
using Renderbuffer = GlObject<deleteRenderbuffer>;

struct Camera
{
   Vec3 _target;
   float _yaw{0.7f};
   float _pitch{0.6f};
   float _distance{20.0f};
   float _fov_y{0.7f};

   [[nodiscard]] Mat4 view() const;
   [[nodiscard]] Vec3 eye() const;
};

/// \brief one tile to draw
struct Instance
{
   uint32_t _tile{0};
   Vec3 _position;
   std::array<float, 3> _tint{1.0f, 1.0f, 1.0f};
   float _turn{0.0f};  //!< extra rotation in radians, only for symmetric tiles
};

class Renderer
{
public:
   /// \brief uploads one mesh per tile of the tileset
   Renderer();

   /// \brief the ground plane under a city of the given size
   void setCitySize(float width, float depth);

   void draw(int32_t width, int32_t height, const Camera& camera, const std::vector<Instance>& instances);

   /// \brief renders offscreen with 4x msaa and writes a png; overlay draws on top, e.g. the ui
   bool screenshot(
      const std::filesystem::path& path,
      int32_t width,
      int32_t height,
      const Camera& camera,
      const std::vector<Instance>& instances,
      const std::function<void()>& overlay = {}
   );

private:
   struct Range
   {
      GLint _first{0};
      GLsizei _count{0};
   };

   void upload();

   Program _program;
   GLint _view_projection_location{-1};
   GLint _offset_location{-1};
   GLint _tint_location{-1};
   GLint _turn_location{-1};
   GLint _eye_location{-1};
   GLint _fog_location{-1};

   VertexArray _vertex_array;
   Buffer _vertices;
   std::vector<Vertex> _data;
   std::vector<Range> _tile_ranges;
   Range _ground_range;
};

}  // namespace testbed
