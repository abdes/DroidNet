//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/Commands/CopyBufferCommand.h>

namespace oxygen::graphics::headless {

void CopyBufferCommand::Execute(CommandContext& /*ctx*/)
{
  auto dst_h = static_cast<Buffer*>(dst_);
  auto src_h = static_cast<const Buffer*>(src_);
  if (dst_h == nullptr || src_h == nullptr) {
    LOG_F(WARNING,
      "Headless CopyBufferCommand: one or both buffers are not "
      "headless-backed");
    return;
  }

  std::vector<std::uint8_t> temp(size_);
  src_h->ReadBacking(temp.data(), src_offset_, size_);
  dst_h->WriteBacking(temp.data(), dst_offset_, size_);
}

} // namespace oxygen::graphics::headless
