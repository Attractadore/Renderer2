#pragma once
#include "Handle.hpp"
#include "Mesh.hpp"
#include "ResourceArena.hpp"

namespace ren {

auto create_persistent_descriptor_set_layout()
    -> AutoHandle<DescriptorSetLayout>;

struct Pipelines {
  Handle<GraphicsPipeline> early_z_pass;
  std::array<Handle<GraphicsPipeline>, NUM_MESH_ATTRIBUTE_FLAGS> opaque_pass;
  Handle<ComputePipeline> post_processing;
  Handle<ComputePipeline> reduce_luminance_histogram;
  Handle<GraphicsPipeline> imgui_pass;
};

auto create_pipeline_layout(ResourceArena &arena,
                            Handle<DescriptorSetLayout> persistent_set_layout,
                            TempSpan<const Span<const std::byte>> shaders,
                            StringView name) -> Handle<PipelineLayout>;

auto load_pipelines(ResourceArena &arena,
                    Handle<DescriptorSetLayout> persistent_set_layout)
    -> Pipelines;

auto load_early_z_pass_pipeline(ResourceArena &arena)
    -> Handle<GraphicsPipeline>;

auto load_opaque_pass_pipelines(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> std::array<Handle<GraphicsPipeline>, NUM_MESH_ATTRIBUTE_FLAGS>;

auto load_post_processing_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline>;

auto load_reduce_luminance_histogram_pipeline(
    ResourceArena &arena, Handle<DescriptorSetLayout> persistent_set_layout)
    -> Handle<ComputePipeline>;

auto load_imgui_pipeline(ResourceArena &arena,
                         Handle<DescriptorSetLayout> textures, VkFormat format)
    -> Handle<GraphicsPipeline>;

} // namespace ren
