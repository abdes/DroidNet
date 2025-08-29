//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Headless/Command.h>

namespace oxygen::graphics::headless {

class CopyBufferCommand : public Command {
public:
  CopyBufferCommand(graphics::Buffer* dst, size_t dst_offset,
    const graphics::Buffer* src, size_t src_offset, size_t size)
    : dst_(dst)
    , dst_offset_(dst_offset)
    , src_(src)
    , src_offset_(src_offset)
    , size_(size)
  {
  }

  auto Execute(CommandContext& ctx) -> void override;

  auto Serialize(std::ostream& os) const -> void override
  {
    // Simple human-readable serialization for now.
    os << "copy_buffer " << dst_ << " " << dst_offset_ << " " << src_ << " "
       << src_offset_ << " " << size_ << "\n";
  }

private:
  graphics::Buffer* dst_;
  size_t dst_offset_;
  const graphics::Buffer* src_;
  size_t src_offset_;
  size_t size_;
};

} // namespace oxygen::graphics::headless
