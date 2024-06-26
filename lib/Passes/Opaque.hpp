#pragma once
#include "Passes/Exposure.hpp"
#include "Support/NotNull.hpp"

namespace ren {

class RgBuilder;
class Scene;

struct OpaquePassesConfig {
  u32 num_meshes = 0;
  u32 num_mesh_instances = 0;
  u32 num_materials = 0;
  u32 num_directional_lights = 0;
  ExposurePassOutput exposure;
};

auto setup_opaque_passes(RgBuilder &rgb, NotNull<const Scene *> scene,
                         const OpaquePassesConfig &cfg) -> RgTextureId;

} // namespace ren
