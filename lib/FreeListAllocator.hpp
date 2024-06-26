#pragma once
#include "Config.hpp"
#include "Support/Vector.hpp"

#include <array>

namespace ren {

class FreeListAllocator {
  unsigned m_top = 1;
  unsigned m_frame_index = 0;
  Vector<unsigned> m_free_list;
  std::array<Vector<unsigned>, PIPELINE_DEPTH> m_frame_freed;

public:
  auto allocate() -> unsigned;

  void free(unsigned idx);

  void next_frame();
};

} // namespace ren
