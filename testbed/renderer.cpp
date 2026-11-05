#include "renderer.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <print>
#include <stdexcept>
#include <string>

namespace testbed
{

void deleteBuffer(GLuint name)
{
   glDeleteBuffers(1, &name);
}

void deleteVertexArray(GLuint name)
{
   glDeleteVertexArrays(1, &name);
}

void deleteProgram(GLuint name)
{
   glDeleteProgram(name);
}

void deleteFramebuffer(GLuint name)
{
   glDeleteFramebuffers(1, &name);
}

void deleteRenderbuffer(GLuint name)
{
   glDeleteRenderbuffers(1, &name);
}

namespace
{

constexpr std::array<float, 3> fog_color{0.70f, 0.78f, 0.86f};

constexpr const char* vertex_shader = R"(#version 300 es
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;  // rgb, and 1 where the building tint applies
uniform mat4 u_view_projection;
uniform vec3 u_offset;
uniform vec3 u_tint;
uniform float u_turn;
out vec3 v_world;
out vec3 v_color;
void main()
{
   float c = cos(u_turn);
   float s = sin(u_turn);
   vec3 p = vec3(c * a_position.x + s * a_position.z, a_position.y, -s * a_position.x + c * a_position.z);
   v_world = p + u_offset;
   v_color = mix(a_color.rgb, a_color.rgb * u_tint, a_color.a);
   gl_Position = u_view_projection * vec4(v_world, 1.0);
}
)";

// flat shading from screen space derivatives, so the meshes need no normals
constexpr const char* fragment_shader = R"(#version 300 es
precision highp float;
in vec3 v_world;
in vec3 v_color;
uniform vec3 u_eye;
uniform vec3 u_fog;
out vec4 o_color;
void main()
{
   vec3 n = normalize(cross(dFdx(v_world), dFdy(v_world)));
   vec3 to_eye = u_eye - v_world;
   if (dot(n, to_eye) < 0.0)
   {
      n = -n;
   }
   vec3 sun = normalize(vec3(0.45, 0.85, 0.3));
   float diffuse = max(dot(n, sun), 0.0);
   float sky = 0.5 + 0.5 * n.y;
   vec3 color = v_color * (0.28 + 0.27 * sky + 0.6 * diffuse);
   float fog = clamp((length(to_eye) - 25.0) / 60.0, 0.0, 0.75);
   o_color = vec4(mix(color, u_fog, fog), 1.0);
}
)";

GLuint compile(GLenum type, const char* source)
{
   const auto shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);
   GLint ok = 0;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (ok == 0)
   {
      std::string log(1024, '\0');
      glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
      glDeleteShader(shader);
      throw std::runtime_error("shader: " + log);
   }
   return shader;
}

/// \brief inverse of rotationX(pitch) * rotationY(yaw), applied to a view space vector
Vec3 unrotate(const Vec3& v, float yaw, float pitch)
{
   const auto cp = std::cos(-pitch);
   const auto sp = std::sin(-pitch);
   const Vec3 a{v.x, cp * v.y - sp * v.z, sp * v.y + cp * v.z};
   const auto cy = std::cos(-yaw);
   const auto sy = std::sin(-yaw);
   return {cy * a.x + sy * a.z, a.y, -sy * a.x + cy * a.z};
}

}  // namespace

Mat4 Camera::view() const
{
   return translation(0.0f, 0.0f, -_distance) * rotationX(_pitch) * rotationY(_yaw) * translation(-_target.x, -_target.y, -_target.z);
}

Vec3 Camera::eye() const
{
   return _target + unrotate({0.0f, 0.0f, _distance}, _yaw, _pitch);
}

Renderer::Renderer()
{
   const auto vs = compile(GL_VERTEX_SHADER, vertex_shader);
   const auto fs = compile(GL_FRAGMENT_SHADER, fragment_shader);
   _program = Program{glCreateProgram()};
   glAttachShader(_program.get(), vs);
   glAttachShader(_program.get(), fs);
   glLinkProgram(_program.get());
   glDeleteShader(vs);
   glDeleteShader(fs);
   GLint ok = 0;
   glGetProgramiv(_program.get(), GL_LINK_STATUS, &ok);
   if (ok == 0)
   {
      throw std::runtime_error("shader program failed to link");
   }
   _view_projection_location = glGetUniformLocation(_program.get(), "u_view_projection");
   _offset_location = glGetUniformLocation(_program.get(), "u_offset");
   _tint_location = glGetUniformLocation(_program.get(), "u_tint");
   _turn_location = glGetUniformLocation(_program.get(), "u_turn");
   _eye_location = glGetUniformLocation(_program.get(), "u_eye");
   _fog_location = glGetUniformLocation(_program.get(), "u_fog");

   GLuint name = 0;
   glGenVertexArrays(1, &name);
   _vertex_array = VertexArray{name};
   glGenBuffers(1, &name);
   _vertices = Buffer{name};

   // every tile mesh in one buffer, drawn by range
   for (const auto& tile : city::tileset()._tiles)
   {
      const auto mesh = tileMesh(tile);
      _tile_ranges.push_back({static_cast<GLint>(_data.size()), static_cast<GLsizei>(mesh.size())});
      _data.insert(_data.end(), mesh.begin(), mesh.end());
   }
   setCitySize(16.0f, 16.0f);
}

void Renderer::setCitySize(float width, float depth)
{
   const auto ground = groundMesh(width * 0.5f, depth * 0.5f);
   _data.resize(static_cast<std::size_t>(_tile_ranges.empty() ? 0 : _tile_ranges.back()._first + _tile_ranges.back()._count));
   _ground_range = {static_cast<GLint>(_data.size()), static_cast<GLsizei>(ground.size())};
   _data.insert(_data.end(), ground.begin(), ground.end());
   upload();
}

void Renderer::upload()
{
   glBindVertexArray(_vertex_array.get());
   glBindBuffer(GL_ARRAY_BUFFER, _vertices.get());
   glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_data.size() * sizeof(Vertex)), _data.data(), GL_STATIC_DRAW);
   constexpr auto stride = static_cast<GLsizei>(sizeof(Vertex));
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, _r)));
   glBindVertexArray(0);
}

void Renderer::draw(int32_t width, int32_t height, const Camera& camera, const std::vector<Instance>& instances)
{
   glViewport(0, 0, width, height);
   glClearColor(fog_color[0], fog_color[1], fog_color[2], 1.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   glDisable(GL_CULL_FACE);
   glUseProgram(_program.get());
   glBindVertexArray(_vertex_array.get());

   const auto aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
   const auto view_projection = perspective(camera._fov_y, aspect, 0.1f, 200.0f) * camera.view();
   const auto eye = camera.eye();
   glUniformMatrix4fv(_view_projection_location, 1, GL_FALSE, view_projection.data());
   glUniform3f(_eye_location, eye.x, eye.y, eye.z);
   glUniform3f(_fog_location, fog_color[0], fog_color[1], fog_color[2]);

   glUniform3f(_offset_location, 0.0f, 0.0f, 0.0f);
   glUniform3f(_tint_location, 1.0f, 1.0f, 1.0f);
   glUniform1f(_turn_location, 0.0f);
   glDrawArrays(GL_TRIANGLES, _ground_range._first, _ground_range._count);

   for (const auto& instance : instances)
   {
      const auto& range = _tile_ranges[instance._tile];
      if (range._count == 0)
      {
         continue;
      }
      glUniform3f(_offset_location, instance._position.x, instance._position.y, instance._position.z);
      glUniform3f(_tint_location, instance._tint[0], instance._tint[1], instance._tint[2]);
      glUniform1f(_turn_location, instance._turn);
      glDrawArrays(GL_TRIANGLES, range._first, range._count);
   }
   glBindVertexArray(0);
}

bool Renderer::screenshot(
   const std::filesystem::path& path,
   int32_t width,
   int32_t height,
   const Camera& camera,
   const std::vector<Instance>& instances,
   const std::function<void()>& overlay
)
{
   GLuint name = 0;
   glGenRenderbuffers(1, &name);
   const Renderbuffer msaa_color{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
   glGenRenderbuffers(1, &name);
   const Renderbuffer msaa_depth{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);
   glGenFramebuffers(1, &name);
   const Framebuffer msaa{name};
   glBindFramebuffer(GL_FRAMEBUFFER, name);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color.get());
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth.get());

   glGenRenderbuffers(1, &name);
   const Renderbuffer resolved_color{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
   glGenFramebuffers(1, &name);
   const Framebuffer resolved{name};
   glBindFramebuffer(GL_FRAMEBUFFER, name);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolved_color.get());

   glBindFramebuffer(GL_FRAMEBUFFER, msaa.get());
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
   {
      std::println(stderr, "offscreen framebuffer is incomplete");
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
   }

   draw(width, height, camera, instances);
   if (overlay)
   {
      overlay();
   }

   glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa.get());
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolved.get());
   glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, resolved.get());
   std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
   glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, 0);

   stbi_flip_vertically_on_write(1);
   return stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

}  // namespace testbed
