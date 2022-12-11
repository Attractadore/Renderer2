#pragma once
#include "PipelineStages.hpp"
#include "Support/Enum.hpp"
#include "Support/Span.hpp"
#include "Support/Vector.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

#include <optional>

namespace ren {
#define REN_RENDER_TARGET_LOAD_OPS (Clear)(Load)(Discard)(None)
REN_DEFINE_ENUM(TargetLoadOp, REN_RENDER_TARGET_LOAD_OPS);

#define REN_RENDER_TARGET_STORE_OPS (Store)(Discard)(None)
REN_DEFINE_ENUM(TargetStoreOp, REN_RENDER_TARGET_STORE_OPS);

struct RenderTargetConfig {
  RenderTargetView rtv;
  TargetLoadOp load_op = TargetLoadOp::Clear;
  TargetStoreOp store_op = TargetStoreOp::Store;
  std::array<float, 4> clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthStencilTargetConfig {
  DepthStencilView dsv;
  TargetLoadOp depth_load_op = TargetLoadOp::Clear;
  TargetStoreOp depth_store_op = TargetStoreOp::Store;
  TargetLoadOp stencil_load_op = TargetLoadOp::None;
  TargetStoreOp stencil_store_op = TargetStoreOp::None;
  float clear_depth = 0.0f;
  uint8_t clear_stencil = 0;
};

#define REN_FILTERS (Nearest)(Linear)
REN_DEFINE_ENUM(Filter, REN_FILTERS);

class CommandBuffer {
public:
  virtual ~CommandBuffer() = default;

  virtual void beginRendering(int x, int y, unsigned width, unsigned height,
                              SmallVector<RenderTargetConfig, 8> rts,
                              std::optional<DepthStencilTargetConfig> dst) = 0;
  void beginRendering(Texture rt) {
    beginRendering(0, 0, rt.desc.width, rt.desc.height,
                   {{.rtv = {.texture = std::move(rt)}}}, {});
  }
  virtual void endRendering() = 0;

  virtual void close() = 0;
};
} // namespace ren
