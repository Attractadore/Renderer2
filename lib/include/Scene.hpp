#pragma once
#include "AssetLoader.hpp"
#include "BufferPool.hpp"
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorSetAllocator.hpp"
#include "Material.hpp"
#include "MaterialAllocator.hpp"
#include "MaterialPipelineCompiler.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "ResourceUploader.hpp"
#include "Support/SlotMap.hpp"
#include "hlsl/interface.hpp"
#include "hlsl/lighting.hpp"
#include "ren/ren.h"

namespace ren {

class Swapchain;

using MeshMap = SlotMap<Mesh>;
using MaterialMap = SlotMap<Material>;
using MeshInstanceMap = SlotMap<Model>;
using DirLightMap = SlotMap<hlsl::DirLight>;

class Scene {
  Device *m_device;

  AssetLoader m_asset_loader;

  BufferPool m_vertex_buffer_pool;
  BufferPool m_index_buffer_pool;
  ResourceUploader m_resource_uploader;
  MaterialPipelineCompiler m_compiler;
  MaterialAllocator m_material_allocator;
  DescriptorSetAllocator m_descriptor_set_allocator;
  CommandAllocator m_cmd_allocator;

  Camera m_camera;

  MeshMap m_meshes;

  MaterialMap m_materials;

  VkFormat m_rt_format = VK_FORMAT_R16G16B16A16_SFLOAT;
  VkFormat m_depth_format = VK_FORMAT_D32_SFLOAT;

public:
  unsigned m_viewport_width = 1280;
  unsigned m_viewport_height = 720;

private:
  MeshInstanceMap m_models;

  PipelineLayout m_pipeline_layout = {};

  DescriptorPool m_persistent_descriptor_pool = {};
  VkDescriptorSet m_persistent_descriptor_set = {};

  BufferRef m_materials_buffer = {};

  DirLightMap m_dir_lights;

private:
  DescriptorSetLayoutRef get_persistent_descriptor_set_layout() const {
    return m_pipeline_layout.desc->set_layouts[hlsl::PERSISTENT_SET];
  }

  DescriptorSetLayoutRef get_global_descriptor_set_layout() const {
    return m_pipeline_layout.desc->set_layouts[hlsl::GLOBAL_SET];
  }

  void next_frame();

public:
  Scene(Device &device);

  RenMesh create_mesh(const RenMeshDesc &desc);

  void create_materials(std::span<const RenMaterialDesc> descs,
                        RenMaterial *out);

  void set_camera(const RenCameraDesc &desc) noexcept;

  void create_mesh_insts(std::span<const RenMeshInstDesc> desc,
                         RenMeshInst *out);
  void destroy_mesh_insts(std::span<const RenMeshInst> mesh_insts) noexcept;

  void set_mesh_inst_matrices(std::span<const RenMeshInst> mesh_insts,
                              std::span<const RenMatrix4x4> matrices) noexcept;

  void create_dir_lights(std::span<const RenDirLightDesc> descs,
                         RenDirLight *out);
  void destroy_dir_lights(std::span<const RenDirLight> lights) noexcept;

  void config_dir_lights(std::span<const RenDirLight> lights,
                         std::span<const RenDirLightDesc> descs);

  void draw(Swapchain &swapchain);
};
} // namespace ren

struct RenScene : ren::Scene {
  using ren::Scene::Scene;
};
