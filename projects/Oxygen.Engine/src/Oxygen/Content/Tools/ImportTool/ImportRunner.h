//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/ImportRequest.h>

namespace oxygen::content::import {
class AsyncImportService; // forward-declared
} // namespace oxygen::content::import

namespace oxygen::content::import::tool {

class IMessageWriter;

[[nodiscard]] auto RunImportJob(const ImportRequest& request,
  oxygen::observer_ptr<IMessageWriter> writer, std::string_view report_path,
  std::string_view command_line, bool enable_tui,
  oxygen::observer_ptr<oxygen::content::import::AsyncImportService> service)
  -> std::expected<void, std::error_code>;

} // namespace oxygen::content::import::tool
