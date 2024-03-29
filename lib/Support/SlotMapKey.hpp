#pragma once
#include "Hash.hpp"
#include "Vector.hpp"

namespace ren {

namespace detail {

struct SlotMapKeyTag {
  constexpr auto operator<=>(const SlotMapKeyTag &other) const = default;
};

} // namespace detail

template <typename K>
concept CSlotMapKey = std::is_base_of_v<detail::SlotMapKeyTag, K>;

class SlotMapKey;

template <typename T, CSlotMapKey K = SlotMapKey,
          template <typename> typename C = Vector>
class DenseSlotMap;

namespace detail {

struct IndexEqualTo;
struct HashIndex;

}; // namespace detail

#define REN_DEFINE_SLOTMAP_KEY(Key)                                            \
  class Key : ::ren::detail::SlotMapKeyTag {                                   \
    template <typename T, ::ren::CSlotMapKey K,                                \
              template <typename> typename C>                                  \
    friend class ::ren::DenseSlotMap;                                          \
    static constexpr size_t index_bits = 24;                                   \
    static constexpr size_t version_bits = 8;                                  \
    static_assert(index_bits + version_bits == 32);                            \
    friend detail::HashIndex;                                                  \
    friend detail::IndexEqualTo;                                               \
                                                                               \
    uint32_t slot : index_bits = 0;                                            \
    uint32_t version : version_bits = 0;                                       \
                                                                               \
    Key(uint32_t index, uint32_t version) : slot(index), version(version) {}   \
                                                                               \
  public:                                                                      \
    constexpr Key() = default;                                                 \
    constexpr auto operator<=>(const Key &other) const = default;              \
    constexpr bool is_null() const noexcept { return *this == Key(); }         \
    constexpr explicit operator bool() const noexcept { return !is_null(); }   \
  };

REN_DEFINE_SLOTMAP_KEY(SlotMapKey);

template <CSlotMapKey K> struct Hash<K> {
  auto operator()(K key) const -> u64 {
    return Hash<u32>()(std::bit_cast<u32>(key));
  }
};

} // namespace ren
