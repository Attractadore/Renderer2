#ifndef REN_GLSL_COMMON_H
#define REN_GLSL_COMMON_H

#if GL_core_profile
#include "Extensions.glsl"
#include "Keywords.glsl"
#include "Util.glsl"

#define GLSL_NAMESPACE_BEGIN
#define GLSL_NAMESPACE_END

#define GLSL_IN(T) T
#define GLSL_OUT(T) out T
#define GLSL_INOUT(T) inout T

#else
#include "Keywords.hpp"

#include <concepts>
#include <cstdint>
#include <glm/glm.hpp>

#define GLSL_NAMESPACE_BEGIN namespace ren::glsl {
#define GLSL_NAMESPACE_END }

#define GLSL_IN(T) const T &
#define GLSL_OUT(T) T &
#define GLSL_INOUT(T) T &

GLSL_NAMESPACE_BEGIN
using namespace glm;
GLSL_NAMESPACE_END
#endif

#endif // REN_GLSL_COMMON_H
