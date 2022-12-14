#include "DirectX12/DirectX12RenderGraph.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12CommandBuffer.hpp"
#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"
#include "Formats.inl"
#include "Support/Errors.hpp"
#include "Support/Views.hpp"

#include <range/v3/action.hpp>

namespace ren {
void DirectX12RenderGraph::Builder::addPresentNodes() {
  auto *dx_swapchain = static_cast<DirectX12Swapchain *>(m_swapchain);

  auto acquire = addNode();
  acquire.setDesc("D3D12: Acquire swapchain buffer");
  m_swapchain_buffer =
      acquire.addExternalTextureOutput({}, PipelineStage::Present);
  setDesc(m_swapchain_buffer, "D3D12: Swapchain buffer");

  auto blit = addNode();
  blit.setDesc("D3D12: Blit final texture to swapchain");
  blit.addReadInput(m_final_image, MemoryAccess::SampledRead,
                    PipelineStage::FragmentShader);
  auto blitted_swapchain_buffer = blit.addWriteInput(
      m_swapchain_buffer, MemoryAccess::ColorWrite, PipelineStage::ColorOutput);
  setDesc(blitted_swapchain_buffer, "D3D12: Blitted swapchain buffer");
  blit.setCallback(
      [final_texture = m_final_image, swapchain_buffer = m_swapchain_buffer,
       root_sig = dx_swapchain->getBlitRootSignature(),
       pso = dx_swapchain->getBlitPSO()](CommandBuffer &cmd, RenderGraph &rg) {
        auto *dx_cmd = static_cast<DirectX12CommandBuffer *>(&cmd);
        auto *dx_device = dx_cmd->getDevice();
        auto *cmd_list = dx_cmd->get();

        auto srv = dx_device->getSRV({
            .desc = {.mip_levels = 1},
            .texture = rg.getTexture(final_texture),
        });
        UINT srv_table_size = 1;
        auto srv_uav_table = dx_cmd->allocateDescriptors(srv_table_size);
        dx_device->get()->CopyDescriptors(
            1, &srv_uav_table.cpu_handle, &srv_table_size, 1, &srv, nullptr,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        const auto &dst_tex = rg.getTexture(swapchain_buffer);
        RenderTargetView rtv = {
            .desc = {.format = getSRGBFormat(dst_tex.desc.format)},
            .texture = dst_tex,
        };
        dx_cmd->beginRendering(std::move(rtv));

        cmd_list->SetGraphicsRootSignature(root_sig);
        cmd_list->SetPipelineState(pso);
        cmd_list->SetGraphicsRootDescriptorTable(0, srv_uav_table.gpu_handle);
        cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd_list->DrawInstanced(3, 1, 0, 0);

        dx_cmd->endRendering();
      });

  auto present = addNode();
  present.setDesc(
      "D3D12: Transition swapchain buffer to D3D12_RESOURCE_STATE_PRESENT");
  present.addReadInput(blitted_swapchain_buffer, {}, PipelineStage::Present);
}

namespace {
D3D12_RESOURCE_STATES
getD3D12ResourceStateFromAccessesAndStages(MemoryAccessFlags accesses,
                                           PipelineStageFlags stages) {
  using enum MemoryAccess;
  using enum PipelineStage;

  // Handle write-only accesses types
  if (accesses == ColorWrite) {
    return D3D12_RESOURCE_STATE_RENDER_TARGET;
  }
  if (accesses == TransferWrite) {
    return D3D12_RESOURCE_STATE_COPY_DEST;
  }

  // Handle read-write accesses types
  if (accesses.isSubset(StorageRead | StorageWrite)) {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if (accesses == DepthWrite) {
    return D3D12_RESOURCE_STATE_DEPTH_WRITE;
  }

  if (accesses.anySet(ColorWrite | TransferWrite | StorageWrite | DepthWrite)) {
    return D3D12_RESOURCE_STATE_COMMON;
  }

  if (stages == Present) {
    return D3D12_RESOURCE_STATE_PRESENT;
  }

  auto states = D3D12_RESOURCE_STATE_COMMON;

  if (accesses.isSet(DepthRead)) {
    states |= D3D12_RESOURCE_STATE_DEPTH_READ;
  }
  if (accesses.isSet(SampledRead)) {
    if (stages.isSet(FragmentShader)) {
      states |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (stages.isSet(ComputeShader)) {
      states |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
  }
  if (accesses == TransferRead) {
    states |= D3D12_RESOURCE_STATE_COPY_SOURCE;
  }

  return states;
}
} // namespace

RGCallback DirectX12RenderGraph::Builder::generateBarrierGroup(
    std::span<const BarrierConfig> configs) {
  auto barriers =
      configs |
      filter_map([](const BarrierConfig &config)
                     -> std::optional<D3D12_RESOURCE_BARRIER> {
        auto state_before = getD3D12ResourceStateFromAccessesAndStages(
            config.src_accesses, config.src_stages);
        auto state_after = getD3D12ResourceStateFromAccessesAndStages(
            config.dst_accesses, config.dst_stages);
        if (state_before == state_after) {
          return std::nullopt;
        }
        return D3D12_RESOURCE_BARRIER{
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Transition = {
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = state_before,
                .StateAfter = state_after,
            }};
      }) |
      ranges::to<SmallVector<D3D12_RESOURCE_BARRIER, 8>>;

  if (barriers.empty()) {
    return [](CommandBuffer &, RenderGraph &) {};
  }

  auto textures = configs |
                  ranges::views::transform([](const BarrierConfig &config) {
                    return config.texture;
                  }) |
                  ranges::to<SmallVector<RGTextureID, 8>>;

  return [textures = std::move(textures), barriers = std::move(barriers)](
             CommandBuffer &cmd, RenderGraph &rg) mutable {
    for (auto &&[texture, barrier] : ranges::views::zip(textures, barriers)) {
      barrier.Transition.pResource = getD3D12Resource(rg.getTexture(texture));
    }
    auto *dx_cmd = static_cast<DirectX12CommandBuffer *>(&cmd);
    dx_cmd->get()->ResourceBarrier(barriers.size(), barriers.data());
  };
}

auto DirectX12RenderGraph::Builder::create_render_graph(
    RenderGraph::Config config) -> std::unique_ptr<RenderGraph> {
  return std::make_unique<DirectX12RenderGraph>(
      std::move(config), Config{
                             .device = static_cast<DirectX12Device *>(m_device),
                             .swapchain_buffer = m_swapchain_buffer,
                         });
}

void DirectX12RenderGraph::execute(CommandAllocator &cmd_allocator) {
  auto &dx_cmd_allocator =
      *static_cast<DirectX12CommandAllocator *>(&cmd_allocator);
  auto &dx_swapchain = *static_cast<DirectX12Swapchain *>(m_swapchain);

  dx_swapchain.AcquireBuffer();
  setTexture(m_swapchain_buffer, dx_swapchain.getTexture());

  SmallVector<ID3D12CommandList *, 16> cmd_lists;
  for (const auto &batch : m_batches) {
    for (auto &&[barrier, pass] :
         ranges::views::zip(batch.barrier_cbs, batch.pass_cbs)) {
      auto &cmd = *dx_cmd_allocator.allocateDirectX12CommandBuffer();
      if (barrier) {
        barrier(cmd, *this);
      }
      if (pass) {
        pass(cmd, *this);
      }
      cmd.close();
      cmd_lists.push_back(cmd.get());
    }

    m_device->directQueueSubmit(cmd_lists);
    cmd_lists.clear();
  }

  dx_swapchain.PresentBuffer();
};
} // namespace ren
