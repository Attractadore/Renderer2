#include "ImGui.hpp"
#if REN_IMGUI
#include "CommandRecorder.hpp"
#include "ImGuiConfig.hpp"
#include "Scene.hpp"
#include "glsl/ImGuiPass.h"

namespace ren {

namespace {

struct ImGuiPassResources {
  RgBufferToken vertices;
  RgBufferToken indices;
};

void run_imgui_pass(Renderer &renderer, const RgRuntime &rg, const Scene &scene,
                    RenderPass &render_pass, const ImGuiPassResources &rcs) {
  ren_ImGuiScope(scene.get_imgui_context());

  const ImDrawData *draw_data = ImGui::GetDrawData();
  if (!draw_data->TotalVtxCount) {
    return;
  }

  {
    auto *vertices = rg.map_buffer<ImDrawVert>(rcs.vertices);
    auto *indices = rg.map_buffer<ImDrawIdx>(rcs.indices);
    for (const ImDrawList *cmd_list : draw_data->CmdLists) {
      std::ranges::copy_n(cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size,
                          vertices);
      vertices += cmd_list->VtxBuffer.Size;
      std::ranges::copy_n(cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size,
                          indices);
      indices += cmd_list->IdxBuffer.Size;
    }
  }

  render_pass.bind_graphics_pipeline(scene.get_pipelines().imgui_pass);

  static_assert(sizeof(ImDrawIdx) * 8 == 16);
  render_pass.bind_index_buffer(rg.get_buffer(rcs.indices),
                                VK_INDEX_TYPE_UINT16);

  render_pass.bind_descriptor_sets({rg.get_texture_set()});

  glm::vec2 clip_offset = {draw_data->DisplayPos.x, draw_data->DisplayPos.y};
  glm::vec2 clip_scale = {draw_data->FramebufferScale.x,
                          draw_data->FramebufferScale.y};

  // Flip viewport
  glm::vec2 display_offset = {draw_data->DisplayPos.x,
                              draw_data->DisplayPos.y +
                                  draw_data->DisplaySize.y};
  glm::vec2 display_size = {draw_data->DisplaySize.x,
                            -draw_data->DisplaySize.y};
  glm::vec2 scale = 2.0f / display_size;
  glm::vec2 translate = glm::vec2(-1.0f) - display_offset * scale;
  glm::vec2 fb_size = scene.get_viewport();

  usize vertex_offset = 0;
  usize index_offset = 0;
  for (const ImDrawList *cmd_list : draw_data->CmdLists) {
    for (const ImDrawCmd &cmd : cmd_list->CmdBuffer) {
      ren_assert(!cmd.UserCallback);

      glm::vec2 clip_min = {cmd.ClipRect.x, cmd.ClipRect.y};
      clip_min = (clip_min - clip_offset) * clip_scale;
      glm::vec2 clip_max = {cmd.ClipRect.z, cmd.ClipRect.w};
      clip_max = (clip_max - clip_offset) * clip_scale;

      clip_min = glm::max(clip_min, 0.0f);
      clip_max = glm::min(clip_max, fb_size);
      if (glm::any(glm::lessThanEqual(clip_max, clip_min))) {
        continue;
      }

      VkRect2D scissor = {
          .offset = {i32(clip_min.x), i32(clip_min.y)},
          .extent = {u32(clip_max.x - clip_min.x),
                     u32(clip_max.y - clip_min.y)},
      };
      render_pass.set_scissor_rects({scissor});

      SampledTextureId texture((uintptr_t)cmd.TextureId);

      render_pass.set_push_constants(glsl::ImGuiPassArgs{
          .vertices = renderer.get_buffer_device_ptr<glsl::ImGuiVertex>(
              rg.get_buffer(rcs.vertices)),
          .scale = scale,
          .translate = translate,
          .tex = texture,
      });

      render_pass.draw_indexed({
          .num_indices = cmd.ElemCount,
          .num_instances = 1,
          .first_index = u32(cmd.IdxOffset + index_offset),
          .vertex_offset = i32(cmd.VtxOffset + vertex_offset),
      });
    }
    index_offset += cmd_list->IdxBuffer.Size;
    vertex_offset += cmd_list->VtxBuffer.Size;
  }
}

} // namespace

auto setup_imgui_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                      const ImGuiPassConfig &cfg) -> RgTextureId {
  ImGuiPassResources rcs;

  auto pass = rgb.create_pass({.name = "imgui"});

  std::tie(std::ignore, rcs.vertices) = pass.create_buffer(
      {
          .name = "imgui-vertices",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(ImDrawVert) * cfg.num_vertices,
      },
      RG_HOST_WRITE_BUFFER | RG_VS_READ_BUFFER);

  std::tie(std::ignore, rcs.indices) = pass.create_buffer(
      {
          .name = "imgui-indices",
          .heap = BufferHeap::Dynamic,
          .size = sizeof(ImDrawIdx) * cfg.num_indices,
      },
      RG_HOST_WRITE_BUFFER | RG_INDEX_BUFFER);

  auto [rt, _] =
      pass.write_color_attachment("imgui", cfg.rt,
                                  {
                                      .load = VK_ATTACHMENT_LOAD_OP_LOAD,
                                      .store = VK_ATTACHMENT_STORE_OP_STORE,
                                  });

  pass.set_graphics_callback(
      [=](Renderer &renderer, const RgRuntime &rt, RenderPass &render_pass) {
        run_imgui_pass(renderer, rt, *scene, render_pass, rcs);
      });

  return rt;
}

} // namespace ren

#endif // REN_IMGUI
