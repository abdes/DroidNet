//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Detail/Publication.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace {
  //! Removes the sibling unless it was published. Declared before the file
  //! and its stream, it is destroyed after them, once the handle is closed.
  struct Sibling {
    std::filesystem::path path;
    bool published = false;

    explicit Sibling(std::filesystem::path sibling_path) noexcept
      : path(std::move(sibling_path))
    {
    }
    OXYGEN_MAKE_NON_COPYABLE(Sibling)
    OXYGEN_MAKE_NON_MOVABLE(Sibling)

    ~Sibling() noexcept
    {
      if (!published) {
        std::error_code error;
        std::filesystem::remove(path, error);
      }
    }
  };

  //! A unique sibling of `path`, in its folder, so the replacement is one
  //! rename on one volume.
  auto SiblingOf(const std::filesystem::path& path) -> std::filesystem::path
  {
    return base::ToNativePath(
      path.parent_path() / (".oxygen-" + Uuid::Generate().ToString() + ".tmp"));
  }

  //! How a sibling is opened: created, never shared, written front to back.
  constexpr auto kSiblingSpec = OpenSpec {
    .access = Access::kWrite,
    .creation = Creation::kCreateNew,
    .sharing = Sharing::kNone,
    .hint = AccessHint::kSequential,
  };

  //! Publishes a closed sibling over `destination`, keeping its permissions.
  auto Publish(Sibling& sibling, const std::filesystem::path& destination,
    const Durability durability) noexcept -> Result<FileReplacement>
  {
    if (const auto access
      = detail::CopyAccessControl(destination, sibling.path);
      !access) {
      return Result<FileReplacement>::Err(access.error());
    }
    auto replaced = MoveFileOver(sibling.path, destination, durability);
    sibling.published = replaced.has_value();
    return replaced;
  }
} // namespace

namespace detail {
  auto ClassifyReplaceError(const std::filesystem::path& from,
    const std::filesystem::path& to, const std::error_code error) noexcept
    -> std::error_code
  {
    auto ignored = std::error_code {};
    if (std::filesystem::is_directory(from, ignored)
      || std::filesystem::is_directory(to, ignored)) {
      return make_error_code(Error::kIsDirectory);
    }
    return error;
  }
} // namespace detail

struct AtomicFileWriter::State {
  State(std::filesystem::path destination_path,
    std::filesystem::path sibling_path, NativeFile opened,
    const Durability durability_asked) noexcept
    : destination(std::move(destination_path))
    , durability(durability_asked)
    , sibling(std::move(sibling_path))
    , file(std::move(opened))
    , stream(file)
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(State)
  OXYGEN_MAKE_NON_MOVABLE(State)
  ~State() noexcept = default;

  std::filesystem::path destination;
  Durability durability;
  // Keep this order. Members are destroyed in reverse: the stream (which
  // writes to `file`) first, then `file`, which closes the handle, and
  // `sibling` last, which can only delete the file once it is closed.
  Sibling sibling;
  NativeFile file;
  RegionWriteStream<NativeFile> stream;
};

AtomicFileWriter::AtomicFileWriter(std::unique_ptr<State> state) noexcept
  : state_(std::move(state))
{
}

AtomicFileWriter::~AtomicFileWriter() noexcept = default;

AtomicFileWriter::AtomicFileWriter(AtomicFileWriter&& other) noexcept = default;

auto AtomicFileWriter::operator=(AtomicFileWriter&& other) noexcept
  -> AtomicFileWriter& = default;

auto AtomicFileWriter::Create(const std::filesystem::path& path,
  const Durability durability) -> Result<AtomicFileWriter>
{
  if (path.filename().empty()) {
    return Result<AtomicFileWriter>::Err(
      std::make_error_code(std::errc::invalid_argument));
  }
  auto temporary = SiblingOf(path);
  auto file = NativeFile::Open(temporary, kSiblingSpec);
  if (!file) {
    return Result<AtomicFileWriter>::Err(file.error());
  }
  return Ok(AtomicFileWriter(std::make_unique<State>(base::ToNativePath(path),
    std::move(temporary), std::move(*file), durability)));
}

auto AtomicFileWriter::Stream() noexcept -> RegionWriteStream<NativeFile>&
{
  return state_->stream;
}

auto AtomicFileWriter::Stream() const noexcept
  -> const RegionWriteStream<NativeFile>&
{
  return state_->stream;
}

auto AtomicFileWriter::Commit() && -> Result<FileReplacement>
{
  const auto state = std::move(state_);
  if (const auto flushed = state->stream.Flush(); !flushed) {
    return Result<FileReplacement>::Err(flushed.error());
  }
  if (const auto synced = state->file.Flush(state->durability); !synced) {
    return Result<FileReplacement>::Err(synced.error());
  }
  // The replacement reopens the sibling by name, and this handle shares
  // nothing: close it. The stream holds no bytes, so it never writes again.
  {
    [[maybe_unused]] const auto closed = std::move(state->file);
  }
  return Publish(state->sibling, state->destination, state->durability);
}

auto WriteFileAtomically(const std::filesystem::path& path,
  const std::span<const std::byte> bytes, const Durability durability)
  -> Result<FileReplacement>
{
  auto writer = AtomicFileWriter::Create(path, durability);
  if (!writer) {
    return Result<FileReplacement>::Err(writer.error());
  }
  if (const auto written = writer->Stream().Write(bytes); !written) {
    return Result<FileReplacement>::Err(written.error());
  }
  return std::move(*writer).Commit();
}

auto AsyncWriteFileAtomically(IoDriver& driver, std::filesystem::path path,
  const std::span<const std::byte> bytes, const Durability durability)
  -> co::Co<Result<FileReplacement>>
{
  if (path.filename().empty()) {
    co_return Result<FileReplacement>::Err(
      std::make_error_code(std::errc::invalid_argument));
  }
  const auto destination = base::ToNativePath(path);
  auto sibling = Sibling(SiblingOf(path));
  {
    auto file = driver.Open(sibling.path, kSiblingSpec);
    if (!file) {
      co_return Result<FileReplacement>::Err(file.error());
    }
    if (const auto written
      = co_await file->AsyncWriteAt(ByteOffset { 0 }, bytes);
      !written) {
      co_return Result<FileReplacement>::Err(written.error());
    }
    if (const auto synced = file->Flush(durability); !synced) {
      co_return Result<FileReplacement>::Err(synced.error());
    }
  } // The replacement reopens the sibling by name: the file closes first.
  co_return Publish(sibling, destination, durability);
}

auto OpenPublishedFile(const std::filesystem::path& path) -> Result<NativeFile>
{
  return NativeFile::Open(path,
    OpenSpec {
      .sharing = Sharing::kReadWriteDelete,
      .hint = AccessHint::kSequential,
    });
}

auto ReadPublishedFile(const std::filesystem::path& path) -> Result<ByteBuffer>
{
  const auto file = OpenPublishedFile(path);
  if (!file) {
    return Result<ByteBuffer>::Err(file.error());
  }
  return ReadAll(*file);
}

} // namespace oxygen::serio
