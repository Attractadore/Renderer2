#pragma once
#include "CommandBuffer.hpp"
#include "Support/ComPtr.hpp"

#include <d3d12.h>

namespace ren {
class DirectX12CommandAllocator;
class DirectX12Device;

class DirectX12CommandBuffer final : public CommandBuffer {
  struct RenderPassInfo {
    D3D12_RECT render_area;
    SmallVector<ID3D12Resource *, 8> discard_resources;
    SmallVector<UINT, 8> discard_subresources;
  };

  DirectX12Device *m_device;
  DirectX12CommandAllocator *m_parent;
  ComPtr<ID3D12GraphicsCommandList> m_cmd_list;
  RenderPassInfo m_current_render_pass;

public:
  DirectX12CommandBuffer(DirectX12Device *device,
                         DirectX12CommandAllocator *parent,
                         ID3D12CommandAllocator *cmd_alloc);
  void wait(SyncObject sync, PipelineStageFlags stages) override;
  void signal(SyncObject sync, PipelineStageFlags stages) override;

  void beginRendering(
      int x, int y, unsigned width, unsigned height,
      SmallVector<RenderTargetConfig, 8> render_targets,
      std::optional<DepthStencilTargetConfig> depth_stencil_target) override;
  void endRendering() override;

  void blit(Texture src, Texture dst, std::span<const BlitRegion> regions,
            Filter filter) override;

  void close() override;
  void reset(ID3D12CommandAllocator *command_allocator);

  ID3D12GraphicsCommandList *get() { return m_cmd_list.Get(); }
};
} // namespace ren