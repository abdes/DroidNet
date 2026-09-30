//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stop_token>

#include <Oxygen/Cooker/api_export.h>

namespace oxygen::co {
class ThreadPool;
}

namespace oxygen::content::import {
class IAsyncFileReader;
class ImportEventLoop;
struct ImportManifest;
struct ImportSourceAnalysis;

namespace detail {
  //! Run analysis with the operation's reader, including captured-source
  //! readers.
  OXGN_COOK_NDAPI auto RunSourceAnalysis(const ImportManifest& manifest,
    ImportEventLoop& loop, IAsyncFileReader& reader, co::ThreadPool& pool,
    std::stop_token stop_token) -> ImportSourceAnalysis;
} // namespace detail
} // namespace oxygen::content::import
