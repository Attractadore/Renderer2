#include "Texture.hpp"
#include "Support/Math.hpp"

namespace ren {

auto get_mip_level_count(unsigned width, unsigned height, unsigned depth)
    -> unsigned {
  auto size = std::max({width, height, depth});
  return ilog2(size) + 1;
}

auto getVkComponentSwizzle(RenTextureChannel channel) -> VkComponentSwizzle {
  switch (channel) {
  case REN_TEXTURE_CHANNEL_IDENTITY:
    return VK_COMPONENT_SWIZZLE_IDENTITY;
  case REN_TEXTURE_CHANNEL_ZERO:
    return VK_COMPONENT_SWIZZLE_ZERO;
  case REN_TEXTURE_CHANNEL_ONE:
    return VK_COMPONENT_SWIZZLE_ONE;
  case REN_TEXTURE_CHANNEL_R:
    return VK_COMPONENT_SWIZZLE_R;
  case REN_TEXTURE_CHANNEL_G:
    return VK_COMPONENT_SWIZZLE_G;
  case REN_TEXTURE_CHANNEL_B:
    return VK_COMPONENT_SWIZZLE_B;
  case REN_TEXTURE_CHANNEL_A:
    return VK_COMPONENT_SWIZZLE_A;
  }
}

auto getVkComponentMapping(const RenTextureChannelSwizzle &swizzle)
    -> VkComponentMapping {
  return {
      .r = getVkComponentSwizzle(swizzle.r),
      .g = getVkComponentSwizzle(swizzle.g),
      .b = getVkComponentSwizzle(swizzle.b),
      .a = getVkComponentSwizzle(swizzle.a),
  };
}

auto getVkFilter(RenFilter filter) -> VkFilter {
  switch (filter) {
  case REN_FILTER_NEAREST:
    return VK_FILTER_NEAREST;
  case REN_FILTER_LINEAR:
    return VK_FILTER_LINEAR;
  }
}

auto getVkSamplerMipmapMode(RenFilter filter) -> VkSamplerMipmapMode {
  switch (filter) {
  case REN_FILTER_NEAREST:
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
  case REN_FILTER_LINEAR:
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
  }
}

auto getVkSamplerAddressMode(RenWrappingMode wrap) -> VkSamplerAddressMode {
  switch (wrap) {
  case REN_WRAPPING_MODE_REPEAT:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case REN_WRAPPING_MODE_MIRRORED_REPEAT:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case REN_WRAPPING_MODE_CLAMP_TO_EDGE:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

} // namespace ren
