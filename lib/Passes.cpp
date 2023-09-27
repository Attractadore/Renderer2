#include "Passes.hpp"
#include "Camera.inl"
#include "Passes/Opaque.hpp"
#include "Passes/PostProcessing.hpp"
#include "Passes/Upload.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "RenderGraph.hpp"
#include "glsl/Lighting.h"
#include "glsl/Material.h"

namespace ren {

namespace {

void setup_all_passes(RgBuilder &rgb, const PassesConfig &cfg) {
  assert(cfg.pipelines);
  assert(cfg.pp_opts);

  setup_upload_pass(rgb);

  auto exposure = setup_exposure_pass(rgb, cfg.pp_opts->exposure);

  setup_opaque_pass(rgb, OpaquePassConfig{
                             .pipeline = cfg.pipelines->opaque_pass,
                             .exposure = exposure,
                             .viewport_size = cfg.viewport_size,
                         });

  setup_post_processing_passes(rgb, PostProcessingPassesConfig{
                                        .pipelines = cfg.pipelines,
                                        .options = cfg.pp_opts,
                                    });

  rgb.present("pp-color-buffer");
}

auto set_all_passes_data(RenderGraph &rg, const PassesData &data) -> bool {
  assert(data.camera);
  assert(data.pp_opts);

#define TRY_SET(...)                                                           \
  do {                                                                         \
    bool valid = __VA_ARGS__;                                                  \
    if (!valid) {                                                              \
      return false;                                                            \
    }                                                                          \
  } while (0)

  TRY_SET(rg.set_pass_data("upload",
                           UploadPassData{
                               .meshes = data.meshes,
                               .materials = data.materials,
                               .mesh_instances = data.mesh_instances,
                               .directional_lights = data.directional_lights,
                           }));

  TRY_SET(set_exposure_pass_data(rg, data.pp_opts->exposure));

  const auto &camera = *data.camera;
  auto size = data.viewport_size;
  auto ar = float(size.x) / float(size.y);
  auto proj = get_projection_matrix(camera, ar);
  auto view =
      glm::lookAt(camera.position, camera.position + camera.forward, camera.up);
  TRY_SET(rg.set_pass_data(
      "opaque", OpaquePassData{
                    .vertex_positions = data.vertex_positions,
                    .vertex_normals = data.vertex_normals,
                    .vertex_tangents = data.vertex_tangents,
                    .vertex_colors = data.vertex_colors,
                    .vertex_uvs = data.vertex_uvs,
                    .vertex_indices = data.vertex_indices,
                    .meshes = data.meshes,
                    .mesh_instances = data.mesh_instances,
                    .viewport_size = size,
                    .proj = proj,
                    .view = view,
                    .eye = camera.position,
                    .num_dir_lights = u32(data.directional_lights.size()),
                }));

  TRY_SET(set_post_processing_passes_data(rg, *data.pp_opts));

#undef TRY_SET

  return true;
}

} // namespace

void update_rg_passes(RenderGraph &rg, CommandAllocator &cmd_alloc,
                      const PassesConfig &cfg, const PassesData &data) {
  bool valid = set_all_passes_data(rg, data);
  if (!valid) {
    RgBuilder rgb(rg);
    setup_all_passes(rgb, cfg);
    rgb.build(cmd_alloc);
    valid = set_all_passes_data(rg, data);
    ren_assert_msg(valid, "Render graph pass data update failed after rebuild");
  }
}

} // namespace ren
