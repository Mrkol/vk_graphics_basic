#ifndef VK_GRAPHICS_BASIC_COMMON_H
#define VK_GRAPHICS_BASIC_COMMON_H

#ifdef __cplusplus
#include <LiteMath.h>
using LiteMath::uint2;
using LiteMath::float2;
using LiteMath::float3;
using LiteMath::float4;
using LiteMath::float4x4;
using LiteMath::make_float2;
using LiteMath::make_float4;

typedef unsigned int uint;
typedef uint2        uvec2;
typedef float4       vec4;
typedef float3       vec3;
typedef float2       vec2;
typedef float4x4     mat4;
#endif

struct UniformParams
{
  vec3  baseColor;
  float time;

  bool animateLightColor;
  char pad0[3];
  float screenWidth;
  float screenHeight;
  char pad1[4];

  mat4  lightMatrix;
  vec3  lightPos;
  char pad2[4];
};

#endif //VK_GRAPHICS_BASIC_COMMON_H
