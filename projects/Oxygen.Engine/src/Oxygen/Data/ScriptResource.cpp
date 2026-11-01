//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Serio/ByteBuffer.h>

using oxygen::data::ScriptResource;

static_assert(std::is_trivially_copyable_v<
                oxygen::data::pak::scripting::ScriptResourceDesc>,
  "ScriptResourceDesc must be trivially copyable");
ScriptResource::ScriptResource(
  oxygen::data::pak::scripting::ScriptResourceDesc desc, serio::ByteBuffer data)
  : desc_(desc)
  , data_(std::move(data))
{
  DCHECK_EQ_F(static_cast<size_t>(desc_.size_bytes), data_.Bytes().size(),
    "ScriptResource payload size mismatch: expected {}, got {}",
    desc_.size_bytes, data_.Bytes().size());
}
