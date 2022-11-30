#include "Vulkan/VulkanDevice.hpp"
#include "Support/Errors.hpp"
#include "Support/Vector.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanRenderGraph.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/VulkanTexture.hpp"

#include <array>

namespace ren {
namespace {
int findQueueFamilyWithCapabilities(VulkanDevice *device, VkQueueFlags caps) {
  unsigned qcnt = 0;
  device->GetPhysicalDeviceQueueFamilyProperties(&qcnt, nullptr);
  SmallVector<VkQueueFamilyProperties, 4> queues(qcnt);
  device->GetPhysicalDeviceQueueFamilyProperties(&qcnt, queues.data());
  for (unsigned i = 0; i < qcnt; ++i) {
    constexpr auto filter =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    if ((queues[i].queueFlags & filter) == caps) {
      return i;
    }
  }
  return -1;
}

int findGraphicsQueueFamily(VulkanDevice *device) {
  return findQueueFamilyWithCapabilities(device, VK_QUEUE_GRAPHICS_BIT |
                                                     VK_QUEUE_COMPUTE_BIT |
                                                     VK_QUEUE_TRANSFER_BIT);
}
} // namespace

VulkanDevice::VulkanDevice(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
                           VkPhysicalDevice m_adapter)
    : m_instance(instance), m_adapter(m_adapter) {
  loadInstanceFunctions(proc, m_instance, &m_vk);

  m_graphics_queue_family = findGraphicsQueueFamily(this);

  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = m_graphics_queue_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };

  VkPhysicalDeviceVulkan12Features vulkan12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .scalarBlockLayout = true,
      .timelineSemaphore = true,
  };

  VkPhysicalDeviceVulkan13Features vulkan13_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &vulkan12_features,
      .synchronization2 = true,
      .dynamicRendering = true,
  };

  std::array extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &vulkan13_features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data(),
  };

  throwIfFailed(CreateDevice(&create_info, &m_device),
                "Vulkan: Failed to create device");

  loadDeviceFunctions(m_vk.GetDeviceProcAddr, m_device, &m_vk);

  GetDeviceQueue(m_graphics_queue_family, 0, &m_graphics_queue);

  VmaVulkanFunctions vma_vulkan_functions = {
      .vkGetInstanceProcAddr = m_vk.GetInstanceProcAddr,
      .vkGetDeviceProcAddr = m_vk.GetDeviceProcAddr,
  };

  VmaAllocatorCreateInfo allocator_info = {
      .physicalDevice = m_adapter,
      .device = m_device,
      .pAllocationCallbacks = getAllocator(),
      .pVulkanFunctions = &vma_vulkan_functions,
      .instance = m_instance,
      .vulkanApiVersion = getRequiredAPIVersion(),
  };

  throwIfFailed(vmaCreateAllocator(&allocator_info, &m_allocator),
                "VMA: Failed to create allocator");
}

VulkanDevice::~VulkanDevice() {
  vmaDestroyAllocator(m_allocator);
  DestroyDevice();
}

Texture VulkanDevice::createTexture(const TextureDesc &desc) {
  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = getVkImageType(desc.type),
      .format = getVkFormat(desc.format),
      .extent = {desc.width, desc.height, desc.depth},
      .mipLevels = desc.levels,
      .arrayLayers = desc.layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = getVkImageUsageFlags(desc.usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

  VkImage image;
  VmaAllocation allocation;
  throwIfFailed(vmaCreateImage(m_allocator, &image_info, &alloc_info, &image,
                               &allocation, nullptr),
                "VMA: Failed to create image");

  return {
      .desc = desc,
      .handle = AnyRef(image,
                       [this, allocation](VkImage image) {
                         vmaDestroyImage(m_allocator, image, allocation);
                         destroyImageViews(image);
                       }),
  };
}

void VulkanDevice::destroyImageViews(VkImage image) {
  for (auto &&[_, view] : m_image_views[image]) {
    DestroyImageView(view);
  }
  m_image_views.erase(image);
}

VkImageView VulkanDevice::getVkImageView(const TextureView &view) {
  auto image = getVkImage(view.texture);
  auto &image_views = m_image_views[image];
  auto it = image_views.find(view.desc);
  if (it != image_views.end()) {
    return it->second;
  }

  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = getVkImageViewType(view.desc.type),
      .format = getVkFormat(view.texture.desc.format),
      .subresourceRange = {
          .aspectMask = getFormatAspectFlags(view.texture.desc.format),
          .baseMipLevel = view.desc.subresource.first_mip_level,
          .levelCount = view.desc.subresource.mip_level_count,
          .baseArrayLayer = view.desc.subresource.first_layer,
          .layerCount = view.desc.subresource.layer_count,
      }};

  VkImageView vk_view;
  throwIfFailed(CreateImageView(&view_info, &vk_view),
                "Vulkan: Failed to create image view");
  image_views.insert(std::pair(view.desc, vk_view));

  return vk_view;
}

VkSemaphore VulkanDevice::createBinarySemaphore() {
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  VkSemaphore semaphore;
  throwIfFailed(CreateSemaphore(&semaphore_info, &semaphore),
                "Vulkan: Failed to create binary semaphore");
  return semaphore;
}

VkSemaphore VulkanDevice::createTimelineSemaphore(uint64_t initial_value) {
  VkSemaphoreTypeCreateInfo semaphore_type_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = initial_value,
  };
  VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphore_type_info,
  };
  VkSemaphore semaphore;
  throwIfFailed(CreateSemaphore(&semaphore_info, &semaphore),
                "Vulkan: Failed to create timeline semaphore");
  return semaphore;
}

SemaphoreWaitResult
VulkanDevice::waitForSemaphore(VkSemaphore sem, uint64_t value,
                               std::chrono::nanoseconds timeout) {
  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &sem,
      .pValues = &value,
  };
  auto r = WaitSemaphores(&wait_info, timeout.count());
  switch (r) {
    using enum SemaphoreWaitResult;
  case VK_SUCCESS:
    return Ready;
  case VK_TIMEOUT:
    return Timeout;
  default:
    throw std::runtime_error{"Vulkan: Failed to wait for semaphore"};
  };
}

std::unique_ptr<RenderGraphBuilder> VulkanDevice::createRenderGraphBuilder() {
  return std::make_unique<VulkanRenderGraphBuilder>(this);
}

std::unique_ptr<CommandAllocator>
VulkanDevice::createCommandBufferPool(unsigned pipeline_depth) {
  return std::make_unique<VulkanCommandAllocator>(this, pipeline_depth);
}

SyncObject VulkanDevice::createSyncObject(const SyncDesc &desc) {
  assert(desc.type == SyncType::Semaphore);
  return {
      .desc = desc,
      .handle = AnyRef(createBinarySemaphore(),
                       [device = this](VkSemaphore semaphore) {
                         device->DestroySemaphore(semaphore);
                       }),
  };
}
} // namespace ren
