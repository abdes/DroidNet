//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::examples::multiview {

//! Compositing mode for picture-in-picture output.
enum class CompositingMode {
  kCopy, //!< CopyTexture-based compositing.
  kBlend, //!< Alpha-blended compositing pass.
};

} // namespace oxygen::examples::multiview
