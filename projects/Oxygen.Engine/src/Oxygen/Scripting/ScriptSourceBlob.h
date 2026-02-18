//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::scripting {

struct ScriptSourceBlob {
  enum class Origin : uint8_t {
    kEmbeddedResource = 0,
    kLooseCookedResource = 1,
    kExternalFile = 2,
  };

  using CanonicalName
    = NamedType<std::string, struct CanonicalNameTag, DefaultInitialized>;

  std::vector<uint8_t> bytes;
  data::pak::ScriptLanguage language { data::pak::ScriptLanguage::kLuau };
  data::pak::ScriptEncoding encoding { data::pak::ScriptEncoding::kSource };
  data::pak::ScriptCompression compression {
    data::pak::ScriptCompression::kNone
  };
  uint64_t content_hash { 0 };
  Origin origin { Origin::kEmbeddedResource };
  CanonicalName canonical_name;

  [[nodiscard]] auto Empty() const noexcept -> bool { return bytes.empty(); }
  [[nodiscard]] auto IsSource() const noexcept -> bool
  {
    return encoding == data::pak::ScriptEncoding::kSource;
  }
  [[nodiscard]] auto IsBytecode() const noexcept -> bool
  {
    return encoding == data::pak::ScriptEncoding::kBytecode;
  }
};

} // namespace oxygen::scripting
