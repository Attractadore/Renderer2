#include "Passes/AutomaticExposure.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "PipelineLoading.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/exposure.hpp"

namespace ren {

auto setup_automatic_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                   const AutomaticExposurePassConfig &cfg)
    -> ExposurePassOutput {
  return cfg.previous_exposure_buffer.map_or_else(
      [&](const RGBufferExportInfo &export_info) -> ExposurePassOutput {
        auto exposure_buffer = rgb.import_buffer({
            .name = "Previous frame's automatic exposure",
            .buffer = export_info.buffer,
            .state = export_info.state,
        });

        return {
            .exposure_buffer = exposure_buffer,
        };
      },
      [&]() -> ExposurePassOutput {
        auto pass = rgb.create_pass({
            .name = "Automatic exposure: set initial exposure",
        });

        auto exposure_buffer = pass.create_buffer({
            .name = "Initial automatic exposure",
            .heap = BufferHeap::Upload,
            .size = sizeof(glsl::Exposure),
        });

        pass.set_callback([exposure_buffer](Device &device, RenderGraph &rg,
                                            CommandBuffer &cmd) {
          auto *exposure_ptr =
              device.map_buffer<glsl::Exposure>(rg.get_buffer(exposure_buffer));
          *exposure_ptr = {
              .exposure = 0.00005f,
          };
        });

        return {
            .exposure_buffer = exposure_buffer,
        };
      });
}

} // namespace ren
