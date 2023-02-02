#pragma once
#include "ResourceUploader.hpp"

namespace ren {
template <ranges::sized_range R>
void ResourceUploader::stage_data(R &&data, const BufferRef &buffer,
                                  unsigned offset) {
  using T = ranges::range_value_t<R>;

  if (auto *out = buffer.map<T>(offset)) {
    ranges::copy(data, out);
    return;
  }

  if (!m_ring_buffer) {
    m_ring_buffer = create_ring_buffer(1 << 20);
  }

  unsigned count = 0;
  while (count < ranges::size(data)) {
    auto [src_offset, num_written] =
        m_ring_buffer->write(ranges::views::drop(data, count));
    if (num_written == 0) {
      m_ring_buffer = create_ring_buffer(2 * m_ring_buffer->size());
    } else {
      m_buffer_copies.push_back(
          {.src = m_ring_buffer->get_buffer(),
           .dst = buffer,
           .region = {
               .srcOffset = src_offset,
               .dstOffset = buffer.desc.offset + offset + count * sizeof(T),
               .size = num_written * sizeof(T),
           }});
      count += num_written;
    }
  }
}
} // namespace ren
