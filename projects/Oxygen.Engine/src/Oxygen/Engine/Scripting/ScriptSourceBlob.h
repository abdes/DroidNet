//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::scripting {

enum class ScriptBlobOrigin : uint8_t {
  kEmbeddedResource = 0,
  kLooseCookedResource = 1,
  kExternalFile = 2,
};

using ScriptBlobCanonicalName = NamedType<std::string,
  struct ScriptBlobCanonicalNameTag, DefaultInitialized>;

//! Container for script source code data.
/*!
  ScriptSourceBlob acts as a unified container for script source data,
  supporting both 'owned' storage (std::vector) and 'referenced' storage
  (std::span).

  ### Design Contracts
  - **Zero-Cost Access**: The data span is cached during construction, making
    BytesView() a simple pointer return (no variant visitation).
  - **Immutable**: Once created, the blob's content cannot be modified.
  - **Origin Tracking**: Maintains metadata about whether the script came from
    a PAK, a loose file, or an external source.
*/
class ScriptSourceBlob final {
public:
  struct OwnedStorage final {
    std::vector<uint8_t> bytes;
  };

  struct ReferencedStorage final {
    std::span<const uint8_t> bytes;
  };

  using Storage = std::variant<OwnedStorage, ReferencedStorage>;

  ScriptSourceBlob(const ScriptSourceBlob&) = delete;
  auto operator=(const ScriptSourceBlob&) -> ScriptSourceBlob& = delete;
  ScriptSourceBlob(ScriptSourceBlob&&) noexcept = default;
  auto operator=(ScriptSourceBlob&&) noexcept -> ScriptSourceBlob& = default;
  ~ScriptSourceBlob() = default;

  [[nodiscard]] static auto FromOwned(std::vector<uint8_t> bytes,
    data::pak::scripting::ScriptLanguage language,
    data::pak::scripting::ScriptCompression compression, uint64_t content_hash,
    ScriptBlobOrigin origin, ScriptBlobCanonicalName canonical_name)
    -> ScriptSourceBlob
  {
    return ScriptSourceBlob(
      Storage { OwnedStorage { .bytes = std::move(bytes) } }, language,
      compression, content_hash, origin, std::move(canonical_name));
  }

  [[nodiscard]] static auto FromReferenced(std::span<const uint8_t> bytes,
    data::pak::scripting::ScriptLanguage language,
    data::pak::scripting::ScriptCompression compression, uint64_t content_hash,
    ScriptBlobOrigin origin, ScriptBlobCanonicalName canonical_name)
    -> ScriptSourceBlob
  {
    return ScriptSourceBlob(Storage { ReferencedStorage { .bytes = bytes } },
      language, compression, content_hash, origin, std::move(canonical_name));
  }

  [[nodiscard]] auto BytesView() const noexcept -> std::span<const uint8_t>
  {
    return bytes_;
  }

  [[nodiscard]] auto IsEmpty() const noexcept -> bool { return bytes_.empty(); }

  [[nodiscard]] auto Size() const noexcept -> size_t { return bytes_.size(); }

  [[nodiscard]] auto IsOwned() const noexcept -> bool
  {
    return std::holds_alternative<OwnedStorage>(storage_);
  }

  [[nodiscard]] auto IsReferenced() const noexcept -> bool
  {
    return std::holds_alternative<ReferencedStorage>(storage_);
  }

  [[nodiscard]] auto Language() const noexcept
    -> data::pak::scripting::ScriptLanguage
  {
    return language_;
  }

  [[nodiscard]] auto Compression() const noexcept
    -> data::pak::scripting::ScriptCompression
  {
    return compression_;
  }

  [[nodiscard]] auto IsCompressed() const noexcept -> bool
  {
    return compression_ != data::pak::scripting::ScriptCompression::kNone;
  }

  [[nodiscard]] auto ContentHash() const noexcept -> uint64_t
  {
    return content_hash_;
  }

  [[nodiscard]] auto GetOrigin() const noexcept -> ScriptBlobOrigin
  {
    return origin_;
  }

  [[nodiscard]] auto GetCanonicalName() const noexcept
    -> const ScriptBlobCanonicalName&
  {
    return canonical_name_;
  }

private:
  ScriptSourceBlob(Storage storage,
    data::pak::scripting::ScriptLanguage language,
    data::pak::scripting::ScriptCompression compression, uint64_t content_hash,
    ScriptBlobOrigin origin, ScriptBlobCanonicalName canonical_name)
    : storage_(std::move(storage))
    , bytes_(std::visit(
        [](const auto& s) { return std::span<const uint8_t>(s.bytes); },
        storage_))
    , language_(language)
    , compression_(compression)
    , content_hash_(content_hash)
    , origin_(origin)
    , canonical_name_(std::move(canonical_name))
  {
  }

  Storage storage_;
  std::span<const uint8_t> bytes_;
  data::pak::scripting::ScriptLanguage language_;
  data::pak::scripting::ScriptCompression compression_;
  uint64_t content_hash_;
  ScriptBlobOrigin origin_;
  ScriptBlobCanonicalName canonical_name_;
};

} // namespace oxygen::scripting
