#pragma once

#include <glm/glm.hpp>
#include <glm/ext.hpp>


struct Camera
{
  Camera() : pos(0.0f, 0.0f, +5.0f), lookAt(0, 0, 0), up(0, 1, 0), fov(45.0f), tdist(100.0f) {}

  glm::vec3 pos;
  glm::vec3 lookAt;
  glm::vec3 up;
  float  fov;
  float  tdist;

  glm::vec3 forward() const { return normalize(lookAt - pos); }
  glm::vec3 right()   const { return cross(forward(), up); }

  void offsetOrientation(float a_upAngle, float rightAngle)
  {
    if (a_upAngle != 0.0f)  // rotate vertical
    {
      glm::vec3 direction =
          normalize(forward() * glm::cos(-glm::radians(a_upAngle)) + up * glm::sin(-glm::radians(a_upAngle)));

      up     = normalize(cross(right(), direction));
      lookAt = pos + tdist*direction;
    }

    if (rightAngle != 0.0f)  // rotate horizontal
    {
      glm::mat3 rot = glm::rotate(glm::identity<glm::mat4>(), -glm::radians(rightAngle), glm::vec3(0, 1, 0));

      glm::vec3 direction2 = glm::normalize(rot * forward());
      up     = normalize(rot * up);
      lookAt = pos + tdist*direction2;
    }
  }

  void offsetPosition(glm::vec3 a_offset)
  {
    pos    += a_offset;
    lookAt += a_offset;
  }
};
