#ifndef REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H
#define REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H

#include "Common.h"
#include "DevicePtr.h"
#include "LuminanceHistogram.h"

GLSL_NAMESPACE_BEGIN

struct ReduceLuminanceHistogramPassArgs {
  GLSL_PTR(LuminanceHistogram) histogram;
  uint exposure_texture;
  float exposure_compensation;
};

const uint REDUCE_LUMINANCE_HISTOGRAM_THREADS_X = NUM_LUMINANCE_HISTOGRAM_BINS;

GLSL_NAMESPACE_END

#endif // REN_GLSL_REDUCE_LUMINANCE_HISTOGRAM_PASS_H
