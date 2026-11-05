#pragma once

// just enough vector and matrix code for an orbit camera; column major like opengl

#include <array>
#include <cmath>
#include <cstddef>

namespace testbed
{

struct Vec3
{
   float x{0.0f};
   float y{0.0f};
   float z{0.0f};

   friend constexpr Vec3 operator+(const Vec3& a, const Vec3& b)
   {
      return {a.x + b.x, a.y + b.y, a.z + b.z};
   }

   friend constexpr Vec3 operator-(const Vec3& a, const Vec3& b)
   {
      return {a.x - b.x, a.y - b.y, a.z - b.z};
   }

   friend constexpr Vec3 operator*(const Vec3& a, float s)
   {
      return {a.x * s, a.y * s, a.z * s};
   }
};

using Mat4 = std::array<float, 16>;

constexpr Mat4 identity()
{
   return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
{
   Mat4 result{};
   for (std::size_t column = 0; column < 4; ++column)
   {
      for (std::size_t row = 0; row < 4; ++row)
      {
         auto sum = 0.0f;
         for (std::size_t k = 0; k < 4; ++k)
         {
            sum += a[k * 4 + row] * b[column * 4 + k];
         }
         result[column * 4 + row] = sum;
      }
   }
   return result;
}

inline Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane)
{
   const auto f = 1.0f / std::tan(fov_y * 0.5f);
   Mat4 m{};
   m[0] = f / aspect;
   m[5] = f;
   m[10] = (far_plane + near_plane) / (near_plane - far_plane);
   m[11] = -1.0f;
   m[14] = 2.0f * far_plane * near_plane / (near_plane - far_plane);
   return m;
}

constexpr Mat4 translation(float x, float y, float z)
{
   auto m = identity();
   m[12] = x;
   m[13] = y;
   m[14] = z;
   return m;
}

inline Mat4 rotationX(float angle)
{
   const auto c = std::cos(angle);
   const auto s = std::sin(angle);
   return {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1};
}

/// \brief right handed rotation around +y: +x turns towards -z, the same turn tinywfc::rotateY applies to sockets
inline Mat4 rotationY(float angle)
{
   const auto c = std::cos(angle);
   const auto s = std::sin(angle);
   return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
}

}  // namespace testbed
