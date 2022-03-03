#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout(location = 0) out vec4 out_fragColor;

layout (location = 0 ) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
  UniformParams Params;
};

layout (binding = 1) uniform sampler2D shadowMap;
layout (binding = 2) uniform sampler2D vsm;


float sq(float x) { return x*x; }

const float innerCosAngle = cos(radians(10.5f));
const float outerCosAngle = cos(radians(20.5f));
const float innerRadius = 0.1f;
const float outerRadius = 10.f;

float shade(vec3 posLightSpaceNDC)
{
  // just shift coords from [-1,1] to [0,1]
  const vec2 shadowTexCoord = posLightSpaceNDC.xy*0.5f + vec2(0.5f, 0.5f);

  const float EPS = 0.0001f;

  const bool outOfView = (shadowTexCoord.x < EPS
    || shadowTexCoord.x > 1.f - EPS
    || shadowTexCoord.y < EPS
    || shadowTexCoord.y > 1.f - EPS);

  if (outOfView)
  {
    return 0.f;
  }

  if (!Params.enableVsm)
  {
    return posLightSpaceNDC.z < textureLod(shadowMap, shadowTexCoord, 0).x + 0.001f ? 1.0f : 0.0f;
  }
  else
  {
    const float t = posLightSpaceNDC.z;

    const float M1 = textureLod(vsm, shadowTexCoord, 0).r;
    const float M2 = textureLod(vsm, shadowTexCoord, 0).g;

    const float mu = M1;
    const float sigma2 = max(M2 - sq(M1), 0.001f);

    const float p = float(t <= mu);
    const float pmax = sigma2 / (sigma2 + sq(t - mu));

    return max(p, pmax);
  }
}

void main()
{
  const vec4 posLightClipSpace = Params.lightMatrix*vec4(surf.wPos, 1.0f); // 
  const vec3 posLightSpaceNDC  = posLightClipSpace.xyz/posLightClipSpace.w;    // for orto matrix, we don't need perspective division, you can remove it if you want; this is general case;
    
  const float shadow = shade(posLightSpaceNDC);

  vec4 lightColor1 = vec4(1.f);

  vec3 spotlightDir = normalize((transpose(Params.lightMatrix) * vec4(0, 0, 1, 0)).xyz);
  vec3 lightDir   = normalize(Params.lightPos - surf.wPos);

  float distToLight = length(Params.lightPos - surf.wPos);
  float cosAngle = dot(-lightDir, spotlightDir);

  float attenuation = clamp((cosAngle - outerCosAngle)/(innerCosAngle - outerCosAngle), 0, 1);

  // from realtime rendering
  float lightSampleDist = mix(innerRadius, outerRadius, 0.05);
  attenuation *=
    lightSampleDist/max(innerRadius, distToLight)
      * sq(max(1 - sq(distToLight / outerRadius), 0));

  vec4 lightColor = max(dot(surf.wNorm, lightDir), 0.0f) * lightColor1 * attenuation;
  out_fragColor   = (lightColor*shadow + vec4(0.1f)) * vec4(Params.baseColor, 1.0f);
}
