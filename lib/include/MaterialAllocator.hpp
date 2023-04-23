#pragma once
#include "Device.hpp"
#include "ResourceUploader.hpp"
#include "Support/FreeListAllocator.hpp"
#include "Support/Span.hpp"
#include "hlsl/material.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace ren {

class MaterialAllocator {
  Device *m_device;
  Buffer m_buffer;
  FreeListAllocator m_allocator;
  Vector<hlsl::Material> m_materials;

  static constexpr auto c_default_capacity = 128;

  Buffer create_buffer(unsigned count) const {
    return m_device->create_buffer({
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .heap = BufferHeap::Device,
        .size = unsigned(count * sizeof(hlsl::Material)),
    });
  }

  unsigned capacity() const { return m_materials.size(); }

  void resize(unsigned new_capacity, ResourceUploader &uploader) {
    m_buffer = create_buffer(new_capacity);
    uploader.stage_data(m_materials, m_buffer);
    m_allocator.expand(new_capacity);
    m_materials.resize(new_capacity);
  }

public:
  MaterialAllocator(Device &device)
      : m_device(&device), m_buffer(create_buffer(c_default_capacity)),
        m_allocator(c_default_capacity), m_materials(c_default_capacity) {}

  unsigned allocate(const RenMaterialDesc &desc, ResourceUploader &uploader) {
    auto alloc = m_allocator.allocate();
    if (!alloc) {
      resize(2 * capacity(), uploader);
      alloc = m_allocator.allocate();
    }
    assert(alloc);
    auto index = *alloc;
    m_materials[index] = {
        .base_color = glm::make_vec4(desc.base_color_factor),
        .metallic = desc.metallic_factor,
        .roughness = desc.roughness_factor,
    };
    uploader.stage_data(asSpan(m_materials[index]), m_buffer,
                        index * sizeof(hlsl::Material));
    return index;
  };

  void free(unsigned index) {
    m_device->push_to_delete_queue(
        [this, index](Device &) { m_allocator.free(index); });
  }

  const Buffer &get_buffer() const { return m_buffer; }
};

} // namespace ren
