#include "PipelineLoading.hpp"
#include "ResourceArena.hpp"
#include "Support/Array.hpp"
#include "Support/Errors.hpp"
#include "glsl/interface.hpp"

#include "FragmentShader.h"
#include "PostProcessingUberShader.h"
#include "ReduceLuminanceHistogramShader.h"
#include "VertexShader.h"

#include "spirv_reflect.h"

namespace ren {

auto create_persistent_descriptor_set_layout(ResourceArena &arena)
    -> Handle<DescriptorSetLayout> {
  std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS> bindings = {};
#if 0
  bindings[glsl::SAMPLERS_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_SAMPLER,
      .count = glsl::NUM_SAMPLERS,
      .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
#endif
  bindings[glsl::SAMPLED_TEXTURES_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .count = glsl::NUM_SAMPLED_TEXTURES,
      .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  bindings[glsl::STORAGE_TEXTURES_SLOT] = {
      .flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .count = glsl::NUM_STORAGE_TEXTURES,
      .stages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
  };
  return arena.create_descriptor_set_layout({
      .name = "Textures descriptor set layout",
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindings = bindings,
  });
}

auto create_pipeline_layout(ResourceArena &arena,
                            Handle<DescriptorSetLayout> persistent_set_layout,
                            TempSpan<const Span<const std::byte>> shaders,
                            std::string_view name) -> Handle<PipelineLayout> {
  VkPushConstantRange push_constants = {};

  for (auto code : shaders) {
    spv_reflect::ShaderModule shader(code.size_bytes(), code.data(),
                                     SPV_REFLECT_MODULE_FLAG_NO_COPY);
    throw_if_failed(shader.GetResult(),
                    "SPIRV-Reflect: Failed to create shader module");

    auto stage = static_cast<VkShaderStageFlagBits>(shader.GetShaderStage());

    uint32_t num_push_constants = 0;
    throw_if_failed(
        shader.EnumeratePushConstantBlocks(&num_push_constants, nullptr),
        "SPIRV-Reflect: Failed to enumerate push constants");
    assert(num_push_constants <= 1);
    if (num_push_constants) {
      SpvReflectBlockVariable *block_var;
      throw_if_failed(
          shader.EnumeratePushConstantBlocks(&num_push_constants, &block_var),
          "SPIRV-Reflect: Failed to enumerate push constants");
      push_constants.stageFlags |= stage;
      push_constants.size = block_var->size;
    }
  }

  SmallVector<Handle<DescriptorSetLayout>> layouts;
  if (persistent_set_layout) {
    layouts.push_back(persistent_set_layout);
  }

  return arena.create_pipeline_layout({
      .name = fmt::format("{} pipeline layout", name),
      .set_layouts = layouts,
      .push_constants = push_constants,
  });
}

auto load_compute_pipeline(ResourceArena &arena,
                           Handle<DescriptorSetLayout> persistent_set_layout,
                           Span<const std::byte> shader, std::string_view name)
    -> Handle<ComputePipeline> {
  auto layout =
      create_pipeline_layout(arena, persistent_set_layout, {shader}, name);
  return arena.create_compute_pipeline({
      .name = fmt::format("{} compute pipeline", name),
      .layout = layout,
      .shader =
          {
              .code = shader,
          },
  });
}

auto load_pipelines(ResourceArena &arena,
                    Handle<DescriptorSetLayout> persistent_set_layout)
    -> Pipelines {
  return {
      .color_pass = load_color_pass_pipeline(arena, persistent_set_layout),
      .post_processing =
          load_post_processing_pipeline(arena, persistent_set_layout),
      .reduce_luminance_histogram =
          load_reduce_luminance_histogram_pipeline(arena),
  };
}

auto load_color_pass_pipeline(ResourceArena &arena,
                              Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<GraphicsPipeline> {
  auto vs = Span(VertexShader, VertexShader_count).as_bytes();
  auto fs = Span(FragmentShader, FragmentShader_count).as_bytes();
  auto layout = create_pipeline_layout(arena, persistent_set_layout, {vs, fs},
                                       "Color pass");
  std::array color_attachments = {ColorAttachmentInfo{
      .format = COLOR_FORMAT,
  }};
  return arena.create_graphics_pipeline({
      .name = "Color pass graphics pipeline",
      .layout = layout,
      .vertex_shader =
          {
              .code = vs,
          },
      .fragment_shader =
          ShaderInfo{
              .code = fs,
          },
      .depth_test =
          DepthTestInfo{
              .format = DEPTH_FORMAT,
          },
      .color_attachments = color_attachments,
  });
}

auto load_reduce_luminance_histogram_pipeline(ResourceArena &arena)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, Handle<DescriptorSetLayout>(),
      Span(ReduceLuminanceHistogramShader, ReduceLuminanceHistogramShader_count)
          .as_bytes(),
      "Reduce luminance histogram");
}

auto load_post_processing_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline> {
  return load_compute_pipeline(
      arena, persistent_set_layout,
      Span(PostProcessingUberShader, PostProcessingUberShader_count).as_bytes(),
      "Post-processing");
}

} // namespace ren
