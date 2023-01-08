#include "PipelineCompiler.hpp"
#include "Config.hpp"
#include "Device.hpp"

#include <boost/container_hash/hash.hpp>
#include <fmt/format.h>

std::size_t std::hash<ren::MaterialConfig>::operator()(
    ren::MaterialConfig const &cfg) const noexcept {
  std::size_t seed = 0;
  boost::hash_combine(seed, cfg.albedo);
  return seed;
}

namespace ren {

namespace {

std::string_view get_albedo_str(MaterialAlbedo albedo) {
  switch (albedo) {
    using enum MaterialAlbedo;
  case Const:
    return "CONST_COLOR";
  case Vertex:
    return "VERTEX_COLOR";
  }
}

struct PipelineConfig {
  PipelineSignatureRef signature;
  std::span<const std::byte> vs;
  std::span<const std::byte> fs;
  Format rt_format;
};

Pipeline compile_pipeline(Device &device, const PipelineConfig &config) {
  return GraphicsPipelineBuilder(device)
      .set_signature(config.signature)
      .set_vertex_shader(config.vs)
      .set_fragment_shader(config.fs)
      .add_render_target(config.rt_format)
      .build();
}
} // namespace

MaterialPipelineCompiler::MaterialPipelineCompiler(
    Device &device, PipelineSignature signature,
    const AssetLoader *asset_loader)
    : m_device(&device), m_signature(std::move(signature)),
      m_asset_loader(asset_loader) {}

auto MaterialPipelineCompiler::get_material_pipeline(
    const MaterialConfig &config, Format rt_format) -> const Pipeline & {
  auto it = m_pipelines.find(config);
  if (it != m_pipelines.end()) {
    return it->second;
  }

  auto get_shader_name = [blob_suffix = m_device->get_shader_blob_suffix()](
                             std::string_view base_name,
                             const MaterialConfig &config) {
    return fmt::format("{0}_{1}{2}", base_name, get_albedo_str(config.albedo),
                       blob_suffix);
  };

  Vector<std::byte> vs, fs;
  m_asset_loader->load_file(get_shader_name("VertexShader", config), vs);
  m_asset_loader->load_file(get_shader_name("FragmentShader", config), fs);

  PipelineConfig pipeline_config = {
      .signature = m_signature,
      .vs = vs,
      .fs = fs,
      .rt_format = rt_format,
  };

  return m_pipelines[config] = compile_pipeline(*m_device, pipeline_config);
}
} // namespace ren
