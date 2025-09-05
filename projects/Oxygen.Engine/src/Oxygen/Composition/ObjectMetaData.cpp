//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetadata.h>

using oxygen::ObjectMetadata;

ObjectMetadata::ObjectMetadata(const std::string_view name)
  : name_(name)
{
  DLOG_F(2, "object name: '{}'", name_);
}

ObjectMetadata::~ObjectMetadata() { DLOG_F(2, "object name: '{}'", name_); }
