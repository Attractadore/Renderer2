#include "Passes.hpp"
#include "Camera.inl"
#include "ImGuiConfig.hpp"
#include "Passes/ImGui.hpp"
#include "Passes/Opaque.hpp"
#include "Passes/PostProcessing.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "RenderGraph.hpp"
#include "glsl/Lighting.h"
#include "glsl/Material.h"

namespace ren {

void setup_render_graph(RgBuilder &rgb, const PassesConfig &cfg) {
  assert(cfg.pipelines);

  auto exposure =
      setup_exposure_pass(rgb, ExposurePassConfig{.mode = cfg.exposure_mode});

  setup_opaque_passes(rgb,
                      OpaquePassesConfig{
                          .pipelines = cfg.pipelines,
                          .num_meshes = cfg.num_meshes,
                          .num_mesh_instances = cfg.num_mesh_instances,
                          .num_materials = cfg.num_materials,
                          .num_directional_lights = cfg.num_directional_lights,
                          .viewport = cfg.viewport,
                          .exposure = exposure,
                          .early_z = cfg.early_z,
                      });

  setup_post_processing_passes(rgb, PostProcessingPassesConfig{
                                        .pipelines = cfg.pipelines,
                                        .exposure_mode = cfg.exposure_mode,
                                        .viewport = cfg.viewport,
                                    });
#if REN_IMGUI
  if (cfg.imgui_context) {
    setup_imgui_pass(rgb, ImGuiPassConfig{
                              .imgui_context = cfg.imgui_context,
                              .pipeline = cfg.pipelines->imgui_pass,
                              .num_vertices = cfg.num_imgui_vertices,
                              .num_indices = cfg.num_imgui_indices,
                              .viewport = cfg.viewport,
                          });
    rgb.present("imgui");
    return;
  }
#endif

  rgb.present("sdr");
}

void update_render_graph(RenderGraph &rg, const PassesConfig &cfg,
                         const PassesRuntimeConfig &rt_cfg) {
  switch (cfg.exposure_mode) {
  case ExposureMode::Camera: {
    *rg.get_parameter<CameraExposureRuntimeConfig>(
        CAMERA_EXPOSURE_RUNTIME_CONFIG) = {
        .options = *rt_cfg.pp_opts.exposure.mode.get<ExposureOptions::Camera>(),
    };
  } break;
  case ExposureMode::Automatic: {
    *rg.get_parameter<AutomaticExposureRuntimeConfig>(
        AUTOMATIC_EXPOSURE_RUNTIME_CONFIG) = {
        .options =
            *rt_cfg.pp_opts.exposure.mode.get<ExposureOptions::Automatic>(),
    };
  } break;
  }

  *rg.get_parameter<SceneRuntimeConfig>(SCENE_RUNTIME_CONFIG) = {
      .camera = rt_cfg.camera,
      .vertex_pool_lists = rt_cfg.vertex_pool_lists,
      .meshes = rt_cfg.meshes,
      .mesh_instances = rt_cfg.mesh_instances,
      .materials = rt_cfg.materials,
      .directional_lights = rt_cfg.directional_lights,
  };

  *rg.get_parameter<InstanceCullingAndLODRuntimeConfig>(
      INSTANCE_CULLING_AND_LOD_RUNTIME_CONFIG) = {
      .lod_bias = rt_cfg.lod_bias,
      .lod_triangle_pixels = rt_cfg.lod_triangle_pixels,
      .frustum_culling = rt_cfg.instance_frustum_culling,
      .lod_selection = rt_cfg.lod_selection,
  };
}

} // namespace ren
