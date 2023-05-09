#pragma once
#include "Support/Hash.hpp"
#include "Support/SlotMapKey.hpp"

namespace ren {

template <typename H> REN_DEFINE_SLOTMAP_KEY(Handle);

template <typename H> struct Hash<Handle<H>> {
  auto operator()(Handle<H> handle) const -> std::size_t {
    auto value = std::bit_cast<unsigned>(handle);
    return Hash<unsigned>()(value);
  }
};

} // namespace ren
