//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {
class IAsyncFileReader;
struct ImportRequest;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Cooks the buffers of a container or a geometry descriptor.
/*!
 Reads each source, validates its row and views, sends it through a
 `BufferPipeline` and emits each result with its sidecar. Submission and
 collection run together, so any number of buffers passes through the bounded
 queues.

 Buffers with equal rows and bytes share one table row, whatever their virtual
 paths; each virtual path has its own sidecar, with its own views.
*/
class BufferImportSubmitter final {
public:
  struct EmittedBuffer final {
    std::string source_id;
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::core::BufferResourceDesc descriptor {};
    std::vector<data::BufferResourceView> views;
    //! The bytes, when asked for by `keep_contents_of`; else null.
    std::shared_ptr<const std::vector<std::byte>> contents;
  };

  struct CookedBuffers final {
    size_t submitted_count = 0;
    std::vector<EmittedBuffer> emitted;
  };

  BufferImportSubmitter(ImportSession& session, const ImportRequest& request,
    observer_ptr<IAsyncFileReader> reader, std::stop_token stop_token);

  //! Cooks `entries` through `pipeline`, whose workers run, and closes it.
  /*!
   @param keep_contents_of The `source_id`s whose bytes the caller reads once
     they are emitted.
  */
  [[nodiscard]] auto CookBuffers(
    std::span<const internal::BufferSource> entries,
    const std::unordered_set<std::string>& keep_contents_of,
    BufferPipeline& pipeline) -> co::Co<CookedBuffers>;

private:
  //! What a buffer's result needs from its submission, by `source_id`.
  struct Submitted final {
    std::unordered_map<std::string, std::vector<data::BufferResourceView>>
      views;
    std::unordered_map<std::string,
      std::shared_ptr<const std::vector<std::byte>>>
      contents;
  };

  // Both run inside `CookBuffers`, which owns what they are given by reference.
  [[nodiscard]] auto SubmitBuffers(
    std::span<const internal::BufferSource> entries,
    const std::unordered_set<std::string>& keep_contents_of,
    BufferPipeline& pipeline, Submitted& submitted, CookedBuffers& cooked)
    -> co::Co<>;

  [[nodiscard]] auto CollectAndEmit(BufferPipeline& pipeline,
    const Submitted& submitted, CookedBuffers& cooked) -> co::Co<>;

  auto Emit(BufferPipeline::WorkResult result, const Submitted& submitted,
    CookedBuffers& cooked) -> void;

  observer_ptr<ImportSession> session_;
  observer_ptr<const ImportRequest> request_;
  observer_ptr<IAsyncFileReader> reader_;
  std::stop_token stop_token_ {};
};

} // namespace oxygen::content::import::detail
