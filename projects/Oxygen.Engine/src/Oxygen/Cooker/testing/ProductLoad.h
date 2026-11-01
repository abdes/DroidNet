//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::cooker::testing::detail {

//! Parses the descriptor `bytes` with the product loader `load`, checking
//! that `references` are exactly the ones its fields use, as a mount does.
/*!
 The builders of the kit check their output with it, so a descriptor the
 product refuses fails where it is built, with the loader's message.

 @throw std::logic_error When the loader refuses the descriptor.
*/
template <typename Load>
auto CheckWithLoader(const std::string_view what,
  const std::span<const std::byte> bytes,
  const data::AssetReferences& references, const Load& load) -> void
{
  auto stream = serio::ReadOnlyMemoryStream { bytes };
  auto reader = serio::Reader<serio::ReadOnlyMemoryStream> { stream };
  try {
    static_cast<void>(load(content::LoaderContext {
      .current_asset_key = {},
      .desc_reader = &reader,
      .asset_references = observer_ptr { &references },
      .work_offline = true,
      .parse_only = true,
    }));
  } catch (const std::exception& refused) {
    throw std::logic_error(
      "the loader refuses the " + std::string(what) + ": " + refused.what());
  }
}

} // namespace oxygen::cooker::testing::detail
