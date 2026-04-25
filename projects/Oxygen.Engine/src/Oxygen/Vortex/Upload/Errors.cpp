//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Upload/Errors.h>

namespace oxygen::vortex::upload {

auto GetUploadErrorCategory() noexcept -> const UploadErrorCategory&
{
  static UploadErrorCategory instance;
  return instance;
}

} // namespace oxygen::vortex::upload
