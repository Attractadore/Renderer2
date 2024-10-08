#pragma once
#include "Support/StdDef.hpp"

#include <vulkan/vulkan.h>

#ifndef REN_IMGUI
#define REN_IMGUI 0
#endif

namespace ren {

constexpr VkFormat HDR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat SDR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr usize MAX_COLOR_ATTACHMENTS = 8;

constexpr usize DESCRIPTOR_TYPE_COUNT = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1;
constexpr usize MAX_DESCIPTOR_BINDINGS = 16;
constexpr usize MAX_DESCRIPTOR_SETS = 4;

constexpr usize MAX_PUSH_CONSTANTS_SIZE = 128;

} // namespace ren
