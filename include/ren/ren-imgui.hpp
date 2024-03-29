#pragma once
#include "ren.hpp"

#include <imgui.h>

namespace ren::imgui {

void set_context(SceneId scene, ImGuiContext *ctx);

auto get_context(SceneId scene) -> ImGuiContext *;

auto draw(SceneId scene) -> expected<void>;

} // namespace ren::imgui
