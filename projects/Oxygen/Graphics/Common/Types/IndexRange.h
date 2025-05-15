//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <Oxygen/Graphics/Common/Detail/DescriptorSpace.h>

namespace Oxygen::Graphics {

/*!
 Represents a range of indices for descriptors.

 Used to map between local descriptor indices (per descriptor type) and global indices.
 Provides methods to check if an index is within the range.
*/
class IndexRange {
 public:
  IndexRange() = default;
  IndexRange(uint32_t base_index, uint32_t count);
  uint32_t BaseIndex() const;
  uint32_t Count() const;
  bool Contains(uint32_t index) const;

 private:
  uint32_t base_index_ = 0; //!< The base (starting) index of the range.
  uint32_t count_ = 0;      //!< The number of indices in the range.
};

}  // namespace Oxygen::Graphics
