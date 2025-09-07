//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/UploadPolicy.h>

namespace oxygen::engine::upload {

auto DefaultUploadPolicy() -> UploadPolicy { return UploadPolicy {}; }

} // namespace oxygen::engine::upload
