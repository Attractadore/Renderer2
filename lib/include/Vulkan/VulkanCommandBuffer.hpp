#pragma once
#include "CommandBuffer.hpp"
#include "Support/Vector.hpp"
#include "VulkanSync.hpp"
#include "VulkanTexture.hpp"

namespace ren {
inline VkAttachmentLoadOp getVkAttachmentLoadOp(TargetLoadOp load_op) {
  using enum TargetLoadOp;
  switch (load_op) {
  case Clear:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  case Discard:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  case None:
  case Load:
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  }
}

REN_MAP_TYPE(TargetStoreOp, VkAttachmentStoreOp);
REN_MAP_FIELD(TargetStoreOp::Store, VK_ATTACHMENT_STORE_OP_STORE);
REN_MAP_FIELD(TargetStoreOp::Discard, VK_ATTACHMENT_STORE_OP_DONT_CARE);
REN_MAP_FIELD(TargetStoreOp::None, VK_ATTACHMENT_STORE_OP_NONE);
REN_MAP_ENUM(getVkAttachmentStoreOp, TargetStoreOp,
             REN_RENDER_TARGET_STORE_OPS);

REN_MAP_TYPE(Filter, VkFilter);
REN_MAP_FIELD(Filter::Nearest, VK_FILTER_NEAREST);
REN_MAP_FIELD(Filter::Linear, VK_FILTER_LINEAR);
REN_MAP_ENUM(getVkFilter, Filter, REN_FILTERS);

class VulkanDevice;
class VulkanCommandAllocator;

class VulkanCommandBuffer final : public CommandBuffer {
  VulkanDevice *m_device;
  VkCommandBuffer m_cmd_buffer;
  VulkanCommandAllocator *m_parent;
  SmallVector<VkSemaphoreSubmitInfo> m_wait_semaphores;
  SmallVector<VkSemaphoreSubmitInfo> m_signal_semaphores;

public:
  VulkanCommandBuffer(VulkanDevice *device, VkCommandBuffer cmd_buffer,
                      VulkanCommandAllocator *parent);

  void beginRendering(
      int x, int y, unsigned width, unsigned height,
      SmallVector<RenderTargetConfig, 8> render_targets,
      std::optional<DepthStencilTargetConfig> depth_stencil_target) override;
  void endRendering() override;

  void copy_buffer(const BufferRef &src, const BufferRef &dst,
                   std::span<const CopyRegion> regions) override;

  void blit(VkImage src, VkImage dst, std::span<const VkImageBlit> regions,
            VkFilter filter);
  void blit(const Texture &src, const Texture &dst) {
    VkImageBlit region = {
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1},
        .srcOffsets = {{}, {int(src.desc.width), int(src.desc.height), 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .layerCount = 1},
        .dstOffsets = {{}, {int(dst.desc.width), int(dst.desc.height), 1}},
    };
    blit(getVkImage(src), getVkImage(dst), asSpan(region), VK_FILTER_LINEAR);
  }

  void wait(SyncObject sync, PipelineStageFlags stages);
  void signal(SyncObject sync, PipelineStageFlags stages);
  std::span<const VkSemaphoreSubmitInfo> getWaitSemaphores() const {
    return m_wait_semaphores;
  }
  std::span<const VkSemaphoreSubmitInfo> getSignalSemaphores() const {
    return m_signal_semaphores;
  }

  void close() override;

  VulkanDevice *getDevice() { return m_device; }
  VkCommandBuffer get() { return m_cmd_buffer; }
};
} // namespace ren
