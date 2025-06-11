//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>

using oxygen::ObjectMetaData;

ObjectMetaData::ObjectMetaData(const std::string_view name)
  : name_(name)
{
  DLOG_F(2, "object '{}' created", name);
}

ObjectMetaData::~ObjectMetaData() { DLOG_F(2, "object '{}' destroyed", name_); }
