//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

class AssetLoader;
namespace internal {
  struct MountReplacementState;
}

//! One content source of a mount set: a loose cooked root or a pak file.
struct ContentMount {
  enum class Kind : std::uint8_t {
    kLooseCookedRoot,
    kPakFile,
  };

  Kind kind { Kind::kLooseCookedRoot };
  //! The root directory or the pak file.
  std::filesystem::path path;

  auto operator==(const ContentMount&) const -> bool = default;
};

//! A validated candidate for one loader's current source context.
class PreparedMountSet final {
public:
  OXGN_CNTT_API ~PreparedMountSet();
  OXYGEN_MAKE_NON_COPYABLE(PreparedMountSet)
  OXGN_CNTT_API PreparedMountSet(PreparedMountSet&&) noexcept;
  OXGN_CNTT_API auto operator=(PreparedMountSet&&) noexcept
    -> PreparedMountSet&;

private:
  friend class AssetLoader;
  explicit PreparedMountSet(
    std::unique_ptr<internal::MountReplacementState> state);
  std::unique_ptr<internal::MountReplacementState> state_;
};

//! Owns old cache entries until the caller finishes switching peer resolvers.
//! Finish or destroy on the loader's owning thread to deliver retirement
//! events.
class MountRetirement final {
public:
  OXGN_CNTT_API ~MountRetirement();
  OXYGEN_MAKE_NON_COPYABLE(MountRetirement)
  OXGN_CNTT_API MountRetirement(MountRetirement&&) noexcept;
  OXGN_CNTT_API auto operator=(MountRetirement&&) noexcept -> MountRetirement&;

  OXGN_CNTT_API auto Finish() noexcept -> void;

private:
  friend class AssetLoader;
  explicit MountRetirement(
    std::unique_ptr<internal::MountReplacementState> state);
  std::unique_ptr<internal::MountReplacementState> state_;
};

} // namespace oxygen::content
