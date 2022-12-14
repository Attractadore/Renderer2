#include "Vulkan/VulkanDevice.hpp"
#include "Support/Array.hpp"
#include "Support/Variant.hpp"
#include "Support/Views.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanDeleteQueue.inl"
#include "Vulkan/VulkanDescriptors.hpp"
#include "Vulkan/VulkanErrors.hpp"
#include "Vulkan/VulkanFormats.hpp"
#include "Vulkan/VulkanPipeline.hpp"
#include "Vulkan/VulkanReflection.hpp"
#include "Vulkan/VulkanRenderGraph.hpp"
#include "Vulkan/VulkanShaderStages.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/VulkanTexture.hpp"

constexpr bool operator==(const VkImageViewCreateInfo &lhs,
                          const VkImageViewCreateInfo &rhs) {
  return lhs.flags == rhs.flags and lhs.viewType == rhs.viewType and
         lhs.format == rhs.format and
         lhs.subresourceRange.aspectMask == rhs.subresourceRange.aspectMask and
         lhs.subresourceRange.baseMipLevel ==
             rhs.subresourceRange.baseMipLevel and
         lhs.subresourceRange.levelCount == rhs.subresourceRange.levelCount and
         lhs.subresourceRange.baseArrayLayer ==
             rhs.subresourceRange.baseArrayLayer and
         lhs.subresourceRange.layerCount == rhs.subresourceRange.layerCount;
}

namespace ren {
std::span<const char *const> VulkanDevice::getRequiredLayers() {
  static constexpr auto layers = makeArray<const char *>(
#if REN_VULKAN_VALIDATION
      "VK_LAYER_KHRONOS_validation"
#endif
  );
  return layers;
}

std::span<const char *const> VulkanDevice::getRequiredExtensions() {
  static constexpr auto extensions = makeArray<const char *>();
  return extensions;
}

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

  VkPhysicalDeviceFeatures2 vulkan10_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .features = {
          .shaderInt64 = true,
      }};

  VkPhysicalDeviceVulkan11Features vulkan11_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &vulkan10_features,
  };

  VkPhysicalDeviceVulkan12Features vulkan12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &vulkan11_features,
      .scalarBlockLayout = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
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
  m_graphics_queue_semaphore = createTimelineSemaphore();

  VmaVulkanFunctions vma_vulkan_functions = {
      .vkGetInstanceProcAddr = m_vk.GetInstanceProcAddr,
      .vkGetDeviceProcAddr = m_vk.GetDeviceProcAddr,
  };

  VmaAllocatorCreateInfo allocator_info = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
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
  waitForIdle();
  m_delete_queue.flush(*this);
  DestroySemaphore(m_graphics_queue_semaphore);
  vmaDestroyAllocator(m_allocator);
  DestroyDevice();
}

void VulkanDevice::begin_frame() {
  m_frame_index = (m_frame_index + 1) % m_frame_end_times.size();
  waitForGraphicsQueue(m_frame_end_times[m_frame_index].graphics_queue_time);
  m_delete_queue.begin_frame(*this);
}

void VulkanDevice::end_frame() {
  m_delete_queue.end_frame(*this);
  m_frame_end_times[m_frame_index].graphics_queue_time = getGraphicsQueueTime();
}

auto VulkanDevice::create_command_allocator(QueueType queue_type)
    -> std::unique_ptr<CommandAllocator> {
  return std::make_unique<VulkanCommandAllocator>(*this);
}

auto VulkanDevice::create_descriptor_pool(const DescriptorPoolDesc &desc)
    -> DescriptorPool {
  StaticVector<VkDescriptorPoolSize, DescriptorCounts::size()> pool_sizes;

#define push_pool_size(r, data, elem)                                          \
  {                                                                            \
    auto type = Descriptor::elem;                                              \
    auto count = desc.descriptor_counts[type];                                 \
    if (count > 0) {                                                           \
      pool_sizes.push_back({                                                   \
          .type = getVkDescriptorType(type),                                   \
          .descriptorCount = count,                                            \
      });                                                                      \
    }                                                                          \
  }

  BOOST_PP_SEQ_FOR_EACH(push_pool_size, ~, REN_DESCRIPTORS);

#undef push_pool_size

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = getVkDescriptorPoolOptionFlags(desc.flags),
      .maxSets = desc.set_count,
      .poolSizeCount = unsigned(pool_sizes.size()),
      .pPoolSizes = pool_sizes.data(),
  };

  VkDescriptorPool pool;
  throwIfFailed(CreateDescriptorPool(&pool_info, &pool),
                "Vulkan: Failed to create descriptor pool");

  return {.desc = desc, .handle = AnyRef(pool, [this](VkDescriptorPool pool) {
                          push_to_delete_queue(pool);
                        })};
}

void VulkanDevice::reset_descriptor_pool(const DescriptorPoolRef &pool) {
  ResetDescriptorPool(getVkDescriptorPool(pool), 0);
}

auto VulkanDevice::create_descriptor_set_layout(
    const DescriptorSetLayoutDesc &desc) -> DescriptorSetLayout {
  auto binding_flags =
      desc.bindings | map([](const DescriptorBinding &binding) {
        return getVkDescriptorBindingOptionFlags(binding.flags);
      }) |
      ranges::to<Vector>;

  auto bindings = desc.bindings | map([](const DescriptorBinding &binding) {
                    return VkDescriptorSetLayoutBinding{
                        .binding = binding.binding,
                        .descriptorType = getVkDescriptorType(binding.type),
                        .descriptorCount = binding.count,
                        .stageFlags = getVkShaderStageFlags(binding.stages),
                    };
                  }) |
                  ranges::to<Vector>;

  VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = unsigned(binding_flags.size()),
      .pBindingFlags = binding_flags.data(),
  };

  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &binding_flags_info,
      .flags = getVkDescriptorSetLayoutOptionFlags(desc.flags),
      .bindingCount = unsigned(bindings.size()),
      .pBindings = bindings.data(),
  };

  VkDescriptorSetLayout layout;
  throwIfFailed(CreateDescriptorSetLayout(&layout_info, &layout),
                "Vulkan: Failed to create descriptor set layout");

  return {.desc = std::make_shared<DescriptorSetLayoutDesc>(desc),
          .handle = AnyRef(layout, [this](VkDescriptorSetLayout layout) {
            push_to_delete_queue(layout);
          })};
}

auto VulkanDevice::allocate_descriptor_sets(
    const DescriptorPoolRef &pool,
    std::span<const DescriptorSetLayoutRef> layouts,
    std::span<DescriptorSet> sets) -> bool {
  assert(sets.size() >= layouts.size());

  auto vk_layouts = layouts | map(getVkDescriptorSetLayout) |
                    ranges::to<SmallVector<VkDescriptorSetLayout>>;
  SmallVector<VkDescriptorSet> vk_sets(vk_layouts.size());

  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = getVkDescriptorPool(pool),
      .descriptorSetCount = unsigned(vk_layouts.size()),
      .pSetLayouts = vk_layouts.data(),
  };

  auto result = AllocateDescriptorSets(&alloc_info, vk_sets.data());
  switch (result) {
  default: {
    throwIfFailed(result, "Vulkan: Failed to allocate descriptor sets");
  }
  case VK_SUCCESS: {
    ranges::transform(vk_sets, sets.data(), [](VkDescriptorSet set) {
      return DescriptorSet{.desc = {}, .handle = set};
    });
    return true;
  }
  case VK_ERROR_FRAGMENTED_POOL:
  case VK_ERROR_OUT_OF_POOL_MEMORY: {
    return false;
  }
  }
}

namespace {
template <Descriptor Type>
auto get_or_empty(const DescriptorSetWriteConfig &config) {
  auto *ptr = std::get_if<DescriptorWriteConfig<Type>>(&config.data);
  return (ptr ? *ptr : DescriptorWriteConfig<Type>()).handles;
}
} // namespace

void VulkanDevice::write_descriptor_sets(
    std::span<const DescriptorSetWriteConfig> configs) {
  auto buffers =
      configs | map([](const DescriptorSetWriteConfig &config)
                        -> std::span<const BufferRef> {
        if (auto *ptr = std::get_if<UniformBufferDescriptors>(&config.data)) {
          return ptr->handles;
        }
        if (auto *ptr = std::get_if<StorageBufferDescriptors>(&config.data)) {
          return ptr->handles;
        }
        return {};
      });

  auto vk_descriptor_buffer_infos = buffers | ranges::views::join |
                                    map([](BufferRef buffer) {
                                      return VkDescriptorBufferInfo{
                                          .buffer = getVkBuffer(buffer),
                                          .offset = buffer.desc.offset,
                                          .range = buffer.desc.size,
                                      };
                                    }) |
                                    ranges::to<Vector>;

  auto buffer_offsets =
      concat(once(0), buffers | map([](std::span<const BufferRef> handles) {
                        return handles.size();
                      }) | ranges::views::partial_sum);

  auto vk_write_descriptor_sets =
      ranges::views::zip(configs, buffer_offsets) | map([&](const auto &t) {
        const auto &[config, buffer_offset] = t;

        VkWriteDescriptorSet vk_config = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = getVkDescriptorSet(config.set),
            .dstBinding = config.binding,
            .dstArrayElement = config.array_index,
        };

        std::visit(
            OverloadSet{
                [&](const SamplerDescriptors &samplers) {
                  vk_config.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                  vkTodo();
                },
                [&, offset = buffer_offset](
                    const UniformBufferDescriptors &uniform_buffers) {
                  vk_config.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                  vk_config.descriptorCount = uniform_buffers.handles.size();
                  vk_config.pBufferInfo =
                      vk_descriptor_buffer_infos.data() + offset;
                },
                [&, offset = buffer_offset](
                    const StorageBufferDescriptors &storage_buffers) {
                  vk_config.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                  vk_config.descriptorCount = storage_buffers.handles.size();
                  vk_config.pBufferInfo =
                      vk_descriptor_buffer_infos.data() + offset;
                },
                [&](const SampledTextureDescriptors &sampled_textures) {
                  vk_config.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                  vk_config.descriptorCount = sampled_textures.handles.size();
                  vkTodo();
                },
                [&](const StorageTextureDescriptors &storage_textures) {
                  vk_config.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                  vk_config.descriptorCount = storage_textures.handles.size();
                  vkTodo();
                },
            },
            config.data);

        return vk_config;
      }) |
      ranges::to<Vector>;

  UpdateDescriptorSets(vk_write_descriptor_sets.size(),
                       vk_write_descriptor_sets.data(), 0, nullptr);
}

Buffer VulkanDevice::create_buffer(const BufferDesc &in_desc) {
  BufferDesc desc = in_desc;
  desc.offset = 0;
  if (desc.size == 0) {
    return {.desc = desc};
  }

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc.size,
      .usage = getVkBufferUsageFlags(desc.usage),
  };

  VmaAllocationCreateInfo alloc_info = {
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO,
  };

  switch (desc.location) {
    using enum BufferLocation;
  case Device:
    alloc_info.flags |=
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    break;
  case Host:
    alloc_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    break;
  case HostCached:
    alloc_info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    break;
  }

  VkBuffer buffer;
  VmaAllocation allocation;
  VmaAllocationInfo map_info;
  throwIfFailed(vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &buffer,
                                &allocation, &map_info),
                "VMA: Failed to create buffer");
  desc.ptr = map_info.pMappedData;

  return {.desc = desc,
          .handle = AnyRef(buffer, [this, allocation](VkBuffer buffer) {
            push_to_delete_queue(buffer);
            push_to_delete_queue(allocation);
          })};
}

auto VulkanDevice::get_buffer_device_address(const BufferRef &buffer) const
    -> uint64_t {
  VkBufferDeviceAddressInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = getVkBuffer(buffer),
  };
  return GetBufferDeviceAddress(&buffer_info);
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

  return {.desc = desc,
          .handle = AnyRef(image, [this, allocation](VkImage image) {
            push_to_delete_queue(VulkanImageViews{image});
            push_to_delete_queue(image);
            push_to_delete_queue(allocation);
          })};
}

void VulkanDevice::destroyImageViews(VkImage image) {
  for (auto &&[_, view] : m_image_views[image]) {
    DestroyImageView(view);
  }
  m_image_views.erase(image);
}

VkImageView VulkanDevice::getVkImageView(const RenderTargetView &rtv) {
  auto image = getVkImage(rtv.texture);
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = getVkFormat(getRTVFormat(rtv)),
      .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = rtv.desc.level,
          .levelCount = 1,
          .baseArrayLayer = rtv.desc.layer,
          .layerCount = 1,
      }};
  return getVkImageViewImpl(image, view_info);
}

VkImageView VulkanDevice::getVkImageView(const DepthStencilView &dsv) {
  auto image = getVkImage(dsv.texture);
  auto format = getDSVFormat(dsv);
  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = getVkFormat(format),
      .subresourceRange = {
          .aspectMask = getFormatAspectFlags(format),
          .baseMipLevel = dsv.desc.level,
          .levelCount = 1,
          .baseArrayLayer = dsv.desc.layer,
          .layerCount = 1,
      }};
  return getVkImageViewImpl(image, view_info);
}

VkImageView
VulkanDevice::getVkImageViewImpl(VkImage image,
                                 const VkImageViewCreateInfo &view_info) {
  if (!image) {
    return VK_NULL_HANDLE;
  }
  auto [it, inserted] = m_image_views[image].insert(view_info, VK_NULL_HANDLE);
  auto &view = std::get<1>(*it);
  if (inserted) {
    throwIfFailed(CreateImageView(&view_info, &view),
                  "Vulkan: Failed to create image view");
  }
  return view;
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
                               std::chrono::nanoseconds timeout) const {
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

std::unique_ptr<RenderGraph::Builder> VulkanDevice::createRenderGraphBuilder() {
  return std::make_unique<VulkanRenderGraph::Builder>(*this);
}

SyncObject VulkanDevice::createSyncObject(const SyncDesc &desc) {
  assert(desc.type == SyncType::Semaphore);
  return {.desc = desc,
          .handle =
              AnyRef(createBinarySemaphore(), [this](VkSemaphore semaphore) {
                push_to_delete_queue(semaphore);
              })};
}

std::unique_ptr<VulkanSwapchain>
VulkanDevice::createSwapchain(VkSurfaceKHR surface) {
  return std::make_unique<VulkanSwapchain>(this, surface);
}

void VulkanDevice::queueSubmitAndSignal(VkQueue queue,
                                        std::span<const VulkanSubmit> submits,
                                        VkSemaphore semaphore, uint64_t value) {
  auto submit_infos =
      submits | map([](const VulkanSubmit &submit) {
        return VkSubmitInfo2{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount = uint32_t(submit.wait_semaphores.size()),
            .pWaitSemaphoreInfos = submit.wait_semaphores.data(),
            .commandBufferInfoCount = uint32_t(submit.command_buffers.size()),
            .pCommandBufferInfos = submit.command_buffers.data(),
            .signalSemaphoreInfoCount =
                uint32_t(submit.signal_semaphores.size()),
            .pSignalSemaphoreInfos = submit.signal_semaphores.data(),
        };
      }) |
      ranges::to<SmallVector<VkSubmitInfo2, 8>>;

  auto final_signal_semaphores =
      concat(submits.back().signal_semaphores,
             once(VkSemaphoreSubmitInfo{
                 .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                 .semaphore = semaphore,
                 .value = value,
             })) |
      ranges::to<SmallVector<VkSemaphoreSubmitInfo, 8>>;

  auto &final_submit_info = submit_infos.back();
  final_submit_info.signalSemaphoreInfoCount = final_signal_semaphores.size();
  final_submit_info.pSignalSemaphoreInfos = final_signal_semaphores.data();

  throwIfFailed(QueueSubmit2(queue, submit_infos.size(), submit_infos.data(),
                             VK_NULL_HANDLE),
                "Vulkan: Failed to submit work to queue");
}

namespace {
VkShaderModule create_shader_module(VulkanDevice &device,
                                    std::span<const std::byte> code) {
  VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size_bytes(),
      .pCode = reinterpret_cast<const uint32_t *>(code.data()),
  };
  VkShaderModule module;
  throwIfFailed(device.CreateShaderModule(&module_info, &module),
                "Vulkan: Failed to create shader module");
  return module;
}
} // namespace

auto VulkanDevice::create_graphics_pipeline(const GraphicsPipelineConfig &desc)
    -> Pipeline {
  SmallVector<VkDynamicState> dynamic_states = {
      VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
      VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
  };

  auto rt_formats = desc.rts |
                    map([](const GraphicsPipelineConfig::RTState &rt) {
                      return getVkFormat(rt.format);
                    }) |
                    ranges::to<SmallVector<VkFormat, 8>>;

  VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = unsigned(rt_formats.size()),
      .pColorAttachmentFormats = rt_formats.data(),
  };

  auto modules =
      desc.shaders |
      map([&](const GraphicsPipelineConfig::ShaderState &shader) {
        VkShaderModuleCreateInfo module_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shader.code.size_bytes(),
            .pCode = reinterpret_cast<const uint32_t *>(shader.code.data()),
        };
        VkShaderModule module;
        throwIfFailed(CreateShaderModule(&module_info, &module),
                      "Vulkan: Failed to create shader module");
        return module;
      }) |
      ranges::to<SmallVector<VkShaderModule, 8>>;

  auto stages =
      ranges::views::zip(desc.shaders, modules) | map([](const auto &p) {
        const auto &[shader, module] = p;
        return VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = getVkShaderStage(shader.stage),
            .module = module,
            .pName = shader.entry_point.c_str(),
        };
      }) |
      ranges::to<SmallVector<VkPipelineShaderStageCreateInfo, 5>>;

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = std::visit(
          [](auto topology) { return getVkPrimitiveTopology(topology); },
          desc.ia.topology),
  };
  if (std::holds_alternative<PrimitiveTopologyType>(desc.ia.topology)) {
    dynamic_states.push_back(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
  }

  VkPipelineViewportStateCreateInfo viewport_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
  };

  VkPipelineRasterizationStateCreateInfo rasterization_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .lineWidth = 1.0f,
  };

  VkSampleMask mask = desc.ms.sample_mask;

  VkPipelineMultisampleStateCreateInfo multisample_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VkSampleCountFlagBits(desc.ms.samples),
      .pSampleMask = &mask,
  };

  VkPipelineColorBlendAttachmentState blend_attachment_info = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment_info,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = unsigned(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data(),
  };

  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = unsigned(stages.size()),
      .pStages = stages.data(),
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly_info,
      .pViewportState = &viewport_info,
      .pRasterizationState = &rasterization_info,
      .pMultisampleState = &multisample_info,
      .pColorBlendState = &blend_info,
      .pDynamicState = &dynamic_state_info,
      .layout = getVkPipelineLayout(desc.signature),
  };

  VkPipeline pipeline;
  throwIfFailed(CreateGraphicsPipelines(nullptr, 1, &pipeline_info, &pipeline),
                "Vulkan: Failed to create graphics pipeline");

  for (auto module : modules) {
    DestroyShaderModule(module);
  }

  return {.handle = AnyRef(pipeline, [this](VkPipeline pipeline) {
            push_to_delete_queue(pipeline);
          })};
}

auto VulkanDevice::create_reflection_module(std::span<const std::byte> data)
    -> std::unique_ptr<ReflectionModule> {
  return std::make_unique<VulkanReflectionModule>(data);
}

auto VulkanDevice::create_pipeline_signature(const PipelineSignatureDesc &desc)
    -> PipelineSignature {
  auto set_layouts = desc.set_layouts | map(getVkDescriptorSetLayout) |
                     ranges::to<SmallVector<VkDescriptorSetLayout, 4>>;

  auto pc_ranges = desc.push_constants |
                   map([](const PushConstantRange &pc_range) {
                     return VkPushConstantRange{
                         .stageFlags = getVkShaderStageFlags(pc_range.stages),
                         .offset = pc_range.offset,
                         .size = pc_range.size,
                     };
                   }) |
                   ranges::to<SmallVector<VkPushConstantRange, 4>>;

  VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = unsigned(set_layouts.size()),
      .pSetLayouts = set_layouts.data(),
      .pushConstantRangeCount = unsigned(pc_ranges.size()),
      .pPushConstantRanges = pc_ranges.data(),
  };

  VkPipelineLayout layout;
  throwIfFailed(CreatePipelineLayout(&layout_info, &layout),
                "Vulkan: Failed to create pipeline layout");

  return {.desc = std::make_unique<PipelineSignatureDesc>(desc),
          .handle = AnyRef(layout, [this](VkPipelineLayout layout) {
            push_to_delete_queue(layout);
          })};
}

} // namespace ren
