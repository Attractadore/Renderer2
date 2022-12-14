#pragma once
#include "Support/Enum.hpp"
#include "Support/Ref.hpp"

#include <span>

namespace ren {
#define REN_BUFFER_USAGES                                                      \
  (TransferSRC)(TransferDST)(Uniform)(Storage)(Index)(Indirect)(DeviceAddress)
REN_DEFINE_FLAGS_ENUM(BufferUsage, REN_BUFFER_USAGES);

enum class BufferLocation {
  Device,
  Host,
  HostCached,
};

struct BufferDesc {
  BufferUsageFlags usage;
  BufferLocation location = BufferLocation::Device;
  unsigned offset = 0;
  unsigned size;
  void *ptr = nullptr;

  bool operator==(const BufferDesc &other) const = default;
};

namespace detail {
template <typename B> class BufferMixin {
  const B &impl() const { return *static_cast<const B *>(this); }
  B &impl() { return *static_cast<B *>(this); }

public:
  template <typename T = std::byte> T *map(unsigned offset = 0) const {
    if (impl().desc.ptr) {
      return reinterpret_cast<T *>(
          reinterpret_cast<std::byte *>(impl().desc.ptr) +
          (impl().desc.offset + offset));
    }
    return nullptr;
  }

  template <typename T = std::byte>
  std::span<T> map(unsigned offset, unsigned count) const {
    assert(impl().desc.ptr);
    return {map<T>(offset), count};
  }

  bool operator==(const BufferMixin &other) const {
    const auto &lhs = impl();
    const auto &rhs = other.impl();
    return lhs.get() == rhs.get() and lhs.desc == rhs.desc;
  };
};
} // namespace detail

struct BufferRef : detail::BufferMixin<BufferRef> {
  BufferDesc desc;
  void *handle;

  void *get() const { return handle; }
};

struct Buffer : detail::BufferMixin<Buffer> {
  BufferDesc desc;
  AnyRef handle;

  operator BufferRef() const {
    return {
        .desc = desc,
        .handle = handle.get(),
    };
  }

  void *get() const { return handle.get(); }
};

template <typename T>
concept BufferLike = std::same_as<T, Buffer> or std::same_as<T, BufferRef>;
} // namespace ren
