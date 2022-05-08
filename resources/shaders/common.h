#ifndef VK_GRAPHICS_BASIC_COMMON_H
#define VK_GRAPHICS_BASIC_COMMON_H

#ifdef __cplusplus
#include <glm/glm.hpp>

#define uint glm::uint
#define uvec2 glm::uvec2
#define vec4 glm::vec4
#define vec3 glm::vec3
#define vec2 glm::vec2
#define mat4 glm::mat4

#define PAD(A, N) char pad##A[N];
#define BOOL uint32_t // Bool is 32 bits in GLSL!!!!
#else
#define BOOL bool
#define PAD(A, N)
#endif

struct UniformParams
{
  vec3  baseColor;
  float time;
  BOOL animateLightColor;
  BOOL enableVsm;
  float screenWidth;
  float screenHeight;
  mat4  lightMatrix;
  vec3  lightPos;
  BOOL enableSsao;
  uint postFxDownscaleFactor;
  uint tonemappingMode;
  float exposure;
  BOOL enableSss;
};

#undef PAD
#undef BOOL


#ifdef __cplusplus
#undef uint
#undef uvec2
#undef vec4
#undef vec3
#undef vec2
#undef mat4
#endif

#endif //VK_GRAPHICS_BASIC_COMMON_H
