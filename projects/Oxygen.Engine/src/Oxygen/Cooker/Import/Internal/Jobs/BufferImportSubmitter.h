//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {
class IAsyncFileReader;
struct ImportRequest;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Reusable submitter for buffer descriptor chunks.
/*!
 Validates `buffers[]` chunks and runtime constraints, submits work to
 `BufferPipeline`, then collects and emits resulting buffer sidecars.
*/
class BufferImportSubmitter final {
public:
  struct Submission final {
    size_t submitted_count = 0;
    std::unordered_map<std::string, std::string>
      descriptor_relpath_by_source_id;
    std::unordered_map<std::string, std::vector<internal::BufferDescriptorView>>
      descriptor_views_by_source_id;
  };

  BufferImportSubmitter(ImportSession& session, const ImportRequest& request,
    observer_ptr<IAsyncFileReader> reader, std::stop_token stop_token);

  [[nodiscard]] auto SubmitBufferChunks(const nlohmann::json& buffer_chunks,
    const std::filesystem::path& descriptor_dir, BufferPipeline& pipeline,
    std::string_view object_path_prefix = "buffers") -> co::Co<Submission>;

  [[nodiscard]] auto CollectAndEmit(
    BufferPipeline& pipeline, const Submission& submission) -> co::Co<>;

private:
  ImportSession& session_;
  const ImportRequest& request_;
  observer_ptr<IAsyncFileReader> reader_ {};
  std::stop_token stop_token_ {};
};

} // namespace oxygen::content::import::detail
