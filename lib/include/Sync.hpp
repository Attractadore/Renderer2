#pragma once
#include "Support/Ref.hpp"

namespace ren {
enum class SyncType;

struct SyncDesc {
  SyncType type;
};

struct SyncObject {
  SyncDesc desc;
  AnyRef handle;
};
}; // namespace ren
