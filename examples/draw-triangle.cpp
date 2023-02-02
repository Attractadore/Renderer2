#include "app-base.hpp"
#include "ren/ren.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

class DrawTriangleApp : public AppBase {
  ren::UniqueMeshID m_mesh;
  ren::UniqueMaterialID m_material;
  ren::UniqueMeshInstanceID m_model;

public:
  DrawTriangleApp() : AppBase("Draw Triangle") {}

protected:
  void iterate(ren::Scene &scene) override {
    if (!m_model) {
      std::array<glm::vec3, 3> positions = {{
          {0.0f, 0.5f, 0.0f},
          {-std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
          {std::sqrt(3.0f) / 4.0f, -0.25f, 0.0f},
      }};

      std::array<glm::vec3, 3> colors = {{
          {1.0f, 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f},
          {0.0f, 0.0f, 1.0f},
      }};

      std::array<unsigned, 3> indices = {0, 1, 2};

      m_mesh =
          scene
              .create_unique_mesh({
                  .positions = std::span(
                      reinterpret_cast<const ren::Vector3 *>(positions.data()),
                      positions.size()),
                  .colors = std::span(
                      reinterpret_cast<const ren::Vector3 *>(colors.data()),
                      colors.size()),
                  .indices = indices,
              })
              .value();

      m_material = scene
                       .create_unique_material({
                           .albedo = ren::VertexMaterialAlbedo(),
                       })
                       .value();

      m_model = scene
                    .create_unique_mesh_instance({
                        .mesh = m_mesh.get(),
                        .material = m_material.get(),
                    })
                    .value();
    }

    scene.set_camera({
        .projection = ren::OrthographicProjection{.width = 2.0f},
        .position = {0.0f, 0.0f, 1.0f},
        .forward = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
    });
  }
};

int main() {
  try {
    DrawTriangleApp().run();
  } catch (const std::exception &e) {
    fmt::print(stderr, "{}\n", e.what());
    return -1;
  }
}
