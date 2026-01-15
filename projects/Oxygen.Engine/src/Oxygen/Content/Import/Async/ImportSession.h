//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedWriter.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::co {
class ThreadPool;
} // namespace oxygen::co

namespace oxygen::content::import {

class IAsyncFileReader;
class IAsyncFileWriter;
class TextureEmitter;
class BufferEmitter;
class AssetEmitter;

//! Per-import-job state including diagnostics and output tracking.
/*!
 The session provides a clean boundary between:
 - AsyncImporter (shared compute infrastructure)
 - Per-job output state (diagnostics, LooseCookedWriter)

 The session owns the `LooseCookedWriter` and collects diagnostics during
 import. Call `Finalize()` to wait for all I/O and write the index file.

 ### Key Features

 - **Thread-Safe Diagnostics**: `AddDiagnostic()` is safe to call from any
   thread (ThreadPool workers, I/O callbacks).
 - **Lazy LooseCookedWriter**: Writer is created on construction pointing at
   the cooked root from the request.
 - **Async Finalization**: `Finalize()` is a coroutine that waits for pending
   I/O and writes the container index file.

 ### Thread Safety

 - Diagnostics collection is thread-safe.
 - Emitter access and use is import-thread only.

 ### Usage Patterns

 ```cpp
 ImportSession session(request, file_reader, file_writer, thread_pool);

 // During import (from ThreadPool workers):
 session.AddDiagnostic({
   .severity = ImportSeverity::kWarning,
   .code = "fbx.missing_texture",
   .message = "Texture not found: diffuse.png",
 });

 // After processing completes:
 auto report = co_await session.Finalize();
 ```

 @see ImportRequest, ImportReport, LooseCookedWriter
*/
class ImportSession final {
public:
  //! Create a session for the given import request.
  /*!
   @param request The import request with source path and layout.
   @param file_reader Async file reader for input operations.
   @param file_writer Async file writer for output operations.
   @param thread_pool Thread pool for CPU-bound work.
  */
  OXGN_CNTT_API ImportSession(const ImportRequest& request,
    oxygen::observer_ptr<IAsyncFileReader> file_reader,
    oxygen::observer_ptr<IAsyncFileWriter> file_writer,
    oxygen::observer_ptr<co::ThreadPool> thread_pool);

  OXGN_CNTT_API ~ImportSession();

  OXYGEN_MAKE_NON_COPYABLE(ImportSession)
  OXYGEN_MAKE_NON_MOVABLE(ImportSession)

  //=== Request Access
  //===-----------------------------------------------------//

  //! Get the original import request.
  OXGN_CNTT_NDAPI auto Request() const noexcept -> const ImportRequest&;

  //! Get the cooked root path for this session.
  OXGN_CNTT_NDAPI auto CookedRoot() const noexcept
    -> const std::filesystem::path&;

  //=== LooseCookedWriter Access
  //===-------------------------------------------//

  //! Get the loose cooked writer for this session.
  OXGN_CNTT_NDAPI auto CookedWriter() noexcept -> LooseCookedWriter&;

  //! Get the async file reader (non-owning).
  OXGN_CNTT_NDAPI auto FileReader() const noexcept
    -> oxygen::observer_ptr<IAsyncFileReader>;

  //! Get the async file writer (non-owning).
  OXGN_CNTT_NDAPI auto FileWriter() const noexcept
    -> oxygen::observer_ptr<IAsyncFileWriter>;

  //! Get the thread pool for CPU-bound work (non-owning).
  OXGN_CNTT_NDAPI auto ThreadPool() const noexcept
    -> oxygen::observer_ptr<co::ThreadPool>;

  //=== Emitters
  //===-----------------------------------------------------------//

  //! Get the texture emitter (lazy, import-thread only).
  OXGN_CNTT_NDAPI auto TextureEmitter()
    -> oxygen::content::import::TextureEmitter&;

  //! Get the buffer emitter (lazy, import-thread only).
  OXGN_CNTT_NDAPI auto BufferEmitter()
    -> oxygen::content::import::BufferEmitter&;

  //! Get the asset emitter (lazy, import-thread only).
  OXGN_CNTT_NDAPI auto AssetEmitter() -> oxygen::content::import::AssetEmitter&;
  //=== Diagnostics
  //===--------------------------------------------------------//

  //! Add a diagnostic message (thread-safe).
  /*!
   May be called from any thread including ThreadPool workers and
   I/O completion callbacks.

   @param diagnostic The diagnostic to add.
  */
  OXGN_CNTT_API auto AddDiagnostic(ImportDiagnostic diagnostic) -> void;

  //! Get all diagnostics collected so far.
  /*!
   @note This takes a lock and copies the diagnostics vector.
         Prefer calling this only during finalization.

   @return Copy of the diagnostics vector.
  */
  OXGN_CNTT_NDAPI auto Diagnostics() const -> std::vector<ImportDiagnostic>;

  //! Check if any error-level diagnostics have been added.
  OXGN_CNTT_NDAPI auto HasErrors() const noexcept -> bool;

  //=== Finalization
  //===-------------------------------------------------------//

  //! Wait for all pending I/O and write the container index file.
  /*!
   This method:
    1. Finalizes any lazily-created emitters (if created)
    2. Waits for any pending async writes to complete
    3. Registers externally-written outputs with `LooseCookedWriter`
    4. Calls `LooseCookedWriter::Finish()` to write `container.index.bin` (last)
    5. Builds and returns an `ImportReport`

   @return Import report with success flag, diagnostics, and asset counts.
  */
  OXGN_CNTT_NDAPI auto Finalize() -> co::Co<ImportReport>;

private:
  ImportRequest request_;
  oxygen::observer_ptr<IAsyncFileReader> file_reader_ {};
  oxygen::observer_ptr<IAsyncFileWriter> file_writer_ {};
  oxygen::observer_ptr<co::ThreadPool> thread_pool_ {};
  std::filesystem::path cooked_root_;
  LooseCookedWriter cooked_writer_;

  std::optional<std::unique_ptr<oxygen::content::import::TextureEmitter>>
    texture_emitter_;
  std::optional<std::unique_ptr<oxygen::content::import::BufferEmitter>>
    buffer_emitter_;
  std::optional<std::unique_ptr<oxygen::content::import::AssetEmitter>>
    asset_emitter_;

  mutable std::mutex diagnostics_mutex_;
  std::vector<ImportDiagnostic> diagnostics_;
  bool has_errors_ = false;
};

} // namespace oxygen::content::import
