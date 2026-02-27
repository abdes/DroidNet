//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>

namespace oxygen::scripting {

//! Container for compiled script bytecode.
/*!
  ScriptBytecodeBlob stores the result of script compilation (Luau bytecode).
  Like ScriptSourceBlob, it supports both owned and referenced storage and
  optimizes access via a cached data span.

  ### Design Contracts
  - **Budget-Aware**: Its Size() is used by the engine's L1 cache to manage
    the scripting memory budget.
  - **Zero-Cost Access**: Optimized BytesView() avoids variant visitation.
  - **Typed Object**: Integrates with the engine's type system for cache
  management.
*/
class ScriptBytecodeBlob final : public oxygen::Object {
  OXYGEN_TYPED(ScriptBytecodeBlob)
public:
  struct OwnedStorage final {
    std::vector<uint8_t> bytes;
  };

  struct ReferencedStorage final {
    std::span<const uint8_t> bytes;
  };

  using Storage = std::variant<OwnedStorage, ReferencedStorage>;

  ScriptBytecodeBlob(const ScriptBytecodeBlob&) = delete;
  auto operator=(const ScriptBytecodeBlob&) -> ScriptBytecodeBlob& = delete;
  ScriptBytecodeBlob(ScriptBytecodeBlob&&) noexcept = default;
  auto operator=(ScriptBytecodeBlob&&) noexcept
    -> ScriptBytecodeBlob& = default;
  ~ScriptBytecodeBlob() = default;

  [[nodiscard]] static auto FromOwned(std::vector<uint8_t> bytes,
    data::pak::scripting::ScriptLanguage language,
    data::pak::scripting::ScriptCompression compression, uint64_t content_hash,
    ScriptBlobOrigin origin, ScriptBlobCanonicalName canonical_name)
    -> ScriptBytecodeBlob
  {
    return ScriptBytecodeBlob(
      Storage { OwnedStorage { .bytes = std::move(bytes) } }, language,
      compression, content_hash, origin, std::move(canonical_name));
  }

  [[nodiscard]] static auto FromReferenced(std::span<const uint8_t> bytes,
    data::pak::scripting::ScriptLanguage language,
    data::pak::scripting::ScriptCompression compression, uint64_t content_hash,
    ScriptBlobOrigin origin, ScriptBlobCanonicalName canonical_name)
    -> ScriptBytecodeBlob
  {
    return ScriptBytecodeBlob(Storage { ReferencedStorage { .bytes = bytes } },
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
  ScriptBytecodeBlob(Storage storage,
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
