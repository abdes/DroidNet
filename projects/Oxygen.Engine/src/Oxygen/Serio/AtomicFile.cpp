//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
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
  //! A unique sibling of a destination: its path, and its file name, which
  //! is ASCII, so a log can name it without converting the path.
  struct SiblingName {
    std::filesystem::path path;
    std::string file_name;
  };

  //! Removes the sibling while it owns it. It owns the file only once the
  //! exclusive create succeeded, so it never removes a file another writer
  //! created, and stops owning it once it is published. Declared before the
  //! file and its stream, it is destroyed after them, once the handle is
  //! closed. A failed removal is logged: the sibling is left behind and
  //! nothing else can report it.
  struct Sibling {
    std::filesystem::path path;
    std::string file_name;
    bool owned = false;

    explicit Sibling(SiblingName name) noexcept
      : path(std::move(name.path))
      , file_name(std::move(name.file_name))
    {
    }
    OXYGEN_MAKE_NON_COPYABLE(Sibling)
    OXYGEN_MAKE_NON_MOVABLE(Sibling)

    ~Sibling() noexcept
    {
      if (!owned) {
        return;
      }
      auto error = std::error_code {};
      std::filesystem::remove(path, error);
      if (error) {
        LOG_F(WARNING, "serio: temporary sibling '{}' left behind: {}",
          file_name, error.message());
      }
    }
  };

  //! A unique sibling of `path`, in its folder, so the replacement is one
  //! rename on one volume.
  auto SiblingOf(const std::filesystem::path& path) -> SiblingName
  {
    auto file_name = ".oxygen-" + Uuid::Generate().ToString() + ".tmp";
    auto sibling = base::ToNativePath(path.parent_path() / file_name);
    return SiblingName {
      .path = std::move(sibling),
      .file_name = std::move(file_name),
    };
  }

  //! How a sibling is opened: created, never shared, written front to back.
  constexpr auto kSiblingSpec = OpenSpec {
    .access = Access::kWrite,
    .creation = Creation::kCreateNew,
    .sharing = Sharing::kNone,
    .hint = AccessHint::kSequential,
  };

  //! Publishes a closed sibling over `destination`, keeping its permissions.
  //! The permission copy makes itself as durable as the data, which was
  //! flushed before it.
  auto Publish(Sibling& sibling, const std::filesystem::path& destination,
    const Durability durability) noexcept -> Result<FileReplacement>
  {
    if (const auto access
      = detail::CopyAccessControl(destination, sibling.path, durability);
      !access) {
      return Result<FileReplacement>::Err(access.error());
    }
    auto replaced = MoveFileOver(sibling.path, destination, durability);
    sibling.owned = !replaced.has_value();
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

  auto FoldersOf(const std::filesystem::path& from,
    const std::filesystem::path& to) -> MovedFolders
  {
    auto folders = MovedFolders {
      .destination = to.parent_path(),
      .source = std::nullopt,
    };
    if (auto source = from.parent_path(); source != folders.destination) {
      folders.source = std::move(source);
    }
    return folders;
  }
} // namespace detail

struct AtomicFileWriter::State {
  State(std::filesystem::path destination_path, SiblingName sibling_name,
    NativeFile opened, const Durability durability_asked) noexcept
    : destination(std::move(destination_path))
    , durability(durability_asked)
    , sibling(std::move(sibling_name))
    , file(std::move(opened))
    , stream(file)
  {
    sibling.owned = true;
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
  auto destination = base::ToNativePath(path);
  auto temporary = SiblingOf(path);
  // Owns the created sibling until the state's own guard does. Declared
  // before the file, so a throw below removes it once the file is closed.
  auto created = Sibling(temporary);
  auto file = NativeFile::Open(temporary.path, kSiblingSpec);
  if (!file) {
    return Result<AtomicFileWriter>::Err(file.error());
  }
  created.owned = true;
  auto state = std::make_unique<State>(
    std::move(destination), std::move(temporary), std::move(*file), durability);
  created.owned = false;
  return Ok(AtomicFileWriter(std::move(state)));
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
      // A name collision fails here, before the guard owns anything.
      co_return Result<FileReplacement>::Err(file.error());
    }
    sibling.owned = true;
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

auto ReadPublishedFile(const std::filesystem::path& path, const ByteCount limit)
  -> Result<ByteBuffer>
{
  const auto file = OpenPublishedFile(path);
  if (!file) {
    return Result<ByteBuffer>::Err(file.error());
  }
  return ReadAll(*file, limit);
}

} // namespace oxygen::serio
