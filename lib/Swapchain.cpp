#include "Swapchain.hpp"
#include "Formats.hpp"
#include "Renderer.hpp"
#include "Support/Errors.hpp"

#include <range/v3/algorithm.hpp>

namespace ren {

namespace {

auto get_surface_capabilities(VkSurfaceKHR surface)
    -> VkSurfaceCapabilitiesKHR {
  VkSurfaceCapabilitiesKHR capabilities;
  throw_if_failed(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                      g_renderer->get_adapter(), surface, &capabilities),
                  "Vulkan: Failed to get surface capabilities");
  return capabilities;
}

auto get_surface_formats(VkPhysicalDevice adapter, VkSurfaceKHR surface) {}

auto select_surface_format(Span<const VkSurfaceFormatKHR> surface_formats)
    -> VkSurfaceFormatKHR {
  auto it = ranges::find_if(surface_formats, [](const VkSurfaceFormatKHR &sf) {
    return isSRGBFormat(sf.format);
  });
  return it != surface_formats.end() ? *it : surface_formats.front();
};

auto select_composite_alpha(VkCompositeAlphaFlagsKHR composite_alphas)
    -> VkCompositeAlphaFlagBitsKHR {
  constexpr std::array preferred_order = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
  };

  for (auto composite_alpha : preferred_order) {
    if (composite_alphas & composite_alpha) {
      return composite_alpha;
    }
  }

  return static_cast<VkCompositeAlphaFlagBitsKHR>(composite_alphas &
                                                  ~(composite_alphas - 1));
}

constexpr VkImageUsageFlags BLIT_STRATEGY_USAGE =
    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
constexpr VkImageUsageFlags RENDER_STRATEGY_USAGE =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

auto select_image_usage(VkImageUsageFlags image_usage) -> VkImageUsageFlags {
  const std::array preferred_order = {
      BLIT_STRATEGY_USAGE,
  };

  for (auto usage : preferred_order) {
    if ((usage & image_usage) == usage) {
      return usage;
    }
  }

  return RENDER_STRATEGY_USAGE;
}

} // namespace

SwapchainImpl::SwapchainImpl(VkSurfaceKHR surface) {
  VkSurfaceCapabilitiesKHR capabilities = get_surface_capabilities(surface);

  SmallVector<VkSurfaceFormatKHR, 8> surface_formats;
  {
    uint32_t num_formats = 0;
    throw_if_failed(
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_renderer->get_adapter(), surface,
                                             &num_formats, nullptr),
        "Vulkan: Failed to get surface formats");
    surface_formats.resize(num_formats);
    throw_if_failed(vkGetPhysicalDeviceSurfaceFormatsKHR(
                        g_renderer->get_adapter(), surface, &num_formats,
                        surface_formats.data()),
                    "Vulkan: Failed to get surface formats");
    surface_formats.resize(num_formats);
  }

  auto num_images = std::max<u32>(capabilities.minImageCount, 2);
  if (capabilities.maxImageCount) {
    num_images = std::min<u32>(capabilities.maxImageCount, num_images);
  }
  VkSurfaceFormatKHR surface_format = select_surface_format(surface_formats);
  VkCompositeAlphaFlagBitsKHR composite_alpha =
      select_composite_alpha(capabilities.supportedCompositeAlpha);
  VkImageUsageFlags image_usage =
      select_image_usage(capabilities.supportedUsageFlags);

  m_create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = num_images,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageArrayLayers = 1,
      .imageUsage = image_usage,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .compositeAlpha = composite_alpha,
      .presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR,
      .clipped = true,
  };

  create();
}

SwapchainImpl::~SwapchainImpl() {
  destroy();
  g_renderer->push_to_delete_queue(m_create_info.surface);
}

void SwapchainImpl::create() {
  VkSurfaceCapabilitiesKHR capabilities =
      get_surface_capabilities(m_create_info.surface);
  m_create_info.imageExtent = [&] {
    if (capabilities.currentExtent.width == 0xFFFFFFFF and
        capabilities.currentExtent.height == 0xFFFFFFFF) {
      return m_create_info.imageExtent;
    } else {
      return capabilities.currentExtent;
    }
  }();
  if (m_create_info.imageExtent.width == 0 or
      m_create_info.imageExtent.height == 0) {
    return;
  }
  m_create_info.preTransform = capabilities.currentTransform;
  m_create_info.oldSwapchain = m_swapchain;

  VkSwapchainKHR new_swapchain;
  throw_if_failed(vkCreateSwapchainKHR(g_renderer->get_device(), &m_create_info,
                                       nullptr, &new_swapchain),
                  "Vulkan: Failed to create swapchain");
  destroy();
  m_swapchain = new_swapchain;

  uint32_t num_images = 0;
  throw_if_failed(vkGetSwapchainImagesKHR(g_renderer->get_device(), m_swapchain,
                                          &num_images, nullptr),
                  "Vulkan: Failed to get swapchain image count");
  SmallVector<VkImage, 3> images(num_images);
  throw_if_failed(vkGetSwapchainImagesKHR(g_renderer->get_device(), m_swapchain,
                                          &num_images, images.data()),
                  "Vulkan: Failed to get swapchain images");

  m_textures.resize(num_images);
  ranges::transform(images, m_textures.begin(), [&](VkImage image) {
    return g_renderer->create_swapchain_texture({
        .image = image,
        .format = m_create_info.imageFormat,
        .usage = m_create_info.imageUsage,
        .width = m_create_info.imageExtent.width,
        .height = m_create_info.imageExtent.height,
    });
  });
}

void SwapchainImpl::destroy() {
  g_renderer->push_to_delete_queue(m_swapchain);
  m_textures.clear();
}

void SwapchainImpl::set_size(unsigned width, unsigned height) {
  m_create_info.imageExtent = {
      .width = width,
      .height = height,
  };
}

void SwapchainImpl::set_present_mode(VkPresentModeKHR present_mode) { todo(); }

auto SwapchainImpl::acquire_texture(Handle<Semaphore> signal_semaphore)
    -> Handle<Texture> {
  while (true) {
    VkResult result = vkAcquireNextImageKHR(
        g_renderer->get_device(), m_swapchain, UINT64_MAX,
        g_renderer->get_semaphore(signal_semaphore).handle, nullptr,
        &m_image_index);
    switch (result) {
    default:
      throw_if_failed(result, "Vulkan: Failed to acquire image");
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      return m_textures[m_image_index];
    case VK_ERROR_OUT_OF_DATE_KHR:
      create();
      continue;
    }
  }
}

void SwapchainImpl::present(Handle<Semaphore> wait_semaphore) {
  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &g_renderer->get_semaphore(wait_semaphore).handle,
      .swapchainCount = 1,
      .pSwapchains = &m_swapchain,
      .pImageIndices = &m_image_index,
  };
  VkResult result = g_renderer->queue_present(present_info);
  switch (result) {
  default:
    throw_if_failed(result, "Vulkan: Failed to present image");
  case VK_SUCCESS:
    return;
  case VK_SUBOPTIMAL_KHR:
  case VK_ERROR_OUT_OF_DATE_KHR:
    create();
    return;
  }
}

} // namespace ren
