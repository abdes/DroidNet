//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/Errors.h>

namespace oxygen::engine::upload {

const UploadErrorCategory& GetUploadErrorCategory() noexcept
{
  static UploadErrorCategory instance;
  return instance;
}

} // namespace oxygen::engine::upload
