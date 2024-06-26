#include "CommandAllocator.hpp"
#include "Renderer.hpp"
#include "Support/Algorithm.hpp"
#include "Support/Errors.hpp"
#include "Support/Views.hpp"

namespace ren {

CommandPool::CommandPool(Renderer &renderer) {
  m_renderer = &renderer;
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = m_renderer->get_graphics_queue_family(),
  };
  throw_if_failed(vkCreateCommandPool(m_renderer->get_device(), &pool_info,
                                      nullptr, &m_pool),
                  "Vulkan: Failed to create command pool");
}

CommandPool::CommandPool(CommandPool &&other) noexcept
    : m_pool(std::exchange(other.m_pool, nullptr)),
      m_cmd_buffers(std::move(other.m_cmd_buffers)),
      m_allocated_count(std::exchange(other.m_allocated_count, 0)) {}

CommandPool &CommandPool::operator=(CommandPool &&other) noexcept {
  destroy();
  m_pool = other.m_pool;
  other.m_pool = nullptr;
  m_cmd_buffers = std::move(other.m_cmd_buffers);
  m_allocated_count = other.m_allocated_count;
  other.m_allocated_count = 0;
  return *this;
}

void CommandPool::destroy() {
  if (m_pool) {
    m_renderer->push_to_delete_queue(
        [pool = m_pool,
         cmd_buffers = std::move(m_cmd_buffers)](Renderer &renderer) {
          if (not cmd_buffers.empty()) {
            vkFreeCommandBuffers(renderer.get_device(), pool,
                                 cmd_buffers.size(), cmd_buffers.data());
          }
          vkDestroyCommandPool(renderer.get_device(), pool, nullptr);
        });
  }
}

CommandPool::~CommandPool() { destroy(); }

VkCommandBuffer CommandPool::allocate() {
  [[unlikely]] if (m_allocated_count == m_cmd_buffers.size()) {
    auto old_capacity = m_cmd_buffers.size();
    auto new_capacity = std::max<size_t>(2 * old_capacity, 1);
    m_cmd_buffers.resize(new_capacity);
    uint32_t alloc_count = new_capacity - old_capacity;
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = alloc_count,
    };
    throw_if_failed(
        vkAllocateCommandBuffers(m_renderer->get_device(), &alloc_info,
                                 m_cmd_buffers.data() + old_capacity),
        "Vulkan: Failed to allocate command buffers");
  }
  return m_cmd_buffers[m_allocated_count++];
}

void CommandPool::reset() {
  throw_if_failed(vkResetCommandPool(m_renderer->get_device(), m_pool, 0),
                  "Vulkan: Failed to reset command pool");
  m_allocated_count = 0;
}

CommandAllocator::CommandAllocator(Renderer &renderer) {
  for (auto i : range(PIPELINE_DEPTH)) {
    m_frame_pools.emplace_back(renderer);
  }
}

void CommandAllocator::next_frame() {
  rotate_left(m_frame_pools);
  m_frame_pools.front().reset();
}

auto CommandAllocator::allocate() -> VkCommandBuffer {
  return m_frame_pools.front().allocate();
}

} // namespace ren
