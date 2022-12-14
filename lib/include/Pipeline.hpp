#pragma once
#include "Def.hpp"
#include "Descriptors.hpp"
#include "Formats.hpp"
#include "ShaderStages.hpp"
#include "Support/LinearMap.hpp"
#include "Support/Ref.hpp"

#include <span>

namespace ren {

struct PipelineDesc {};

struct PipelineRef {
  PipelineDesc desc;
  void *handle;

  void *get() const { return handle; }
};

struct Pipeline {
  PipelineDesc desc;
  AnyRef handle;

  operator PipelineRef() const {
    return {.desc = desc, .handle = handle.get()};
  }

  void *get() const { return handle.get(); }
};

struct PushConstantRange {
  ShaderStageFlags stages;
  unsigned offset = 0;
  unsigned size;
};

struct PipelineSignatureDesc {
  SmallVector<DescriptorSetLayout, 4> set_layouts;
  SmallVector<PushConstantRange> push_constants;
};

struct PipelineSignatureRef {
  PipelineSignatureDesc *desc;
  void *handle;

  void *get() const { return handle; }
};

struct PipelineSignature {
  std::shared_ptr<PipelineSignatureDesc> desc;
  AnyRef handle;

  operator PipelineSignatureRef() const {
    return {.desc = desc.get(), .handle = handle.get()};
  }

  void *get() const { return handle.get(); }
};

#define REN_PRIMITIVE_TOPOLOGY_TYPES (Points)(Lines)(Triangles)
REN_DEFINE_ENUM(PrimitiveTopologyType, REN_PRIMITIVE_TOPOLOGY_TYPES);

#define REN_PRIMITIVE_TOPOLOGIES                                               \
  (PointList)(LineList)(                                                       \
      LineStrip)(TriangleList)(TriangleStrip)(LineListWithAdjacency)(LineStripWithAdjacency)(TriangleListWithAdjacency)(TriangleStripWithAdjacency)
REN_DEFINE_ENUM(PrimitiveTopology, REN_PRIMITIVE_TOPOLOGIES);

struct GraphicsPipelineConfig {
  PipelineSignatureRef signature;

  struct ShaderState {
    ShaderStage stage;
    std::span<const std::byte> code;
    std::string entry_point;
  };
  StaticVector<ShaderState, 2> shaders;

  struct IAState {
    std::variant<PrimitiveTopologyType, PrimitiveTopology> topology =
        PrimitiveTopology::TriangleList;
  } ia;

  struct MSState {
    unsigned samples = 1;
    unsigned sample_mask = -1;
  } ms;

  struct RTState {
    Format format;
  };
  StaticVector<RTState, 8> rts;
};

class GraphicsPipelineBuilder {
  Device *m_device;
  GraphicsPipelineConfig m_desc = {};

public:
  explicit GraphicsPipelineBuilder(Device &device) : m_device(&device) {}

  auto set_signature(const PipelineSignatureRef &signature)
      -> GraphicsPipelineBuilder & {
    m_desc.signature = signature;
    return *this;
  }

  auto set_shader(ShaderStage stage, std::span<const std::byte> code,
                  std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    m_desc.shaders.push_back({
        .stage = stage,
        .code = code,
        .entry_point = std::string(entry_point),
    });
    return *this;
  }

  auto set_vertex_shader(std::span<const std::byte> code,
                         std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    return set_shader(ShaderStage::Vertex, code, entry_point);
  }

  auto set_fragment_shader(std::span<const std::byte> code,
                           std::string_view entry_point = "main")
      -> GraphicsPipelineBuilder & {
    return set_shader(ShaderStage::Fragment, code, entry_point);
  }

  auto add_render_target(Format format) -> GraphicsPipelineBuilder & {
    m_desc.rts.push_back({.format = format});
    return *this;
  }

  auto build() -> Pipeline;
};

} // namespace ren
