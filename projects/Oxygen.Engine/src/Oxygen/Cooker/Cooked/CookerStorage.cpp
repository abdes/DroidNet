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
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::cooked {

namespace {

  namespace fs = std::filesystem;

  //! A publication through `serio::AtomicFileWriter`.
  class AtomicPublication final : public Publication {
  public:
    explicit AtomicPublication(serio::AtomicFileWriter writer) noexcept
      : writer_(std::move(writer))
    {
    }

    [[nodiscard]] auto Write(const std::span<const std::byte> in) noexcept
      -> Result<void> override
    {
      return Writer().Stream().Write(in);
    }

    [[nodiscard]] auto Flush() noexcept -> Result<void> override
    {
      return Writer().Stream().Flush();
    }

    [[nodiscard]] auto Position() const noexcept -> serio::ByteOffset override
    {
      return writer_ ? writer_->Stream().Position() : serio::ByteOffset { 0 };
    }

    [[nodiscard]] auto PatchAt(const serio::ByteOffset at,
      const std::span<const std::byte> in) noexcept -> Result<void> override
    {
      return Writer().Stream().PatchAt(at, in);
    }

    [[nodiscard]] auto Commit() noexcept -> Result<void> override
    {
      if (!writer_) {
        return ::oxygen::Err(
          std::make_error_code(std::errc::bad_file_descriptor));
      }
      auto writer = std::move(*writer_);
      writer_.reset();
      const auto replaced = std::move(writer).Commit();
      if (!replaced) {
        return ::oxygen::Err(replaced.error());
      }
      // Published, so the commit succeeded; only its durability is unknown.
      if (replaced->durability_error) {
        LOG_F(WARNING, "Cooked file committed; directory durability failed: {}",
          replaced->durability_error.message());
      }
      return {};
    }

  private:
    auto Writer() noexcept -> serio::AtomicFileWriter&
    {
      // A spent publication streams nowhere: Commit is its last call.
      return *writer_; // NOLINT(bugprone-unchecked-optional-access)
    }

    std::optional<serio::AtomicFileWriter> writer_;
  };

  //! An operating-system lock held for the storage's caller.
  class HeldFileLock final : public StorageLock {
  public:
    explicit HeldFileLock(serio::FileLock lock) noexcept
      : lock_(std::move(lock))
    {
    }

  private:
    serio::FileLock lock_;
  };

  class RealFiles final : public ImportStorage {
  public:
    [[nodiscard]] auto KindOf(const fs::path& path) const -> PathKind override
    {
      auto ec = std::error_code {};
      const auto status = fs::status(path, ec);
      if (fs::is_directory(status)) {
        return PathKind::kFolder;
      }
      return fs::exists(status) ? PathKind::kFile : PathKind::kNone;
    }

    [[nodiscard]] auto Open(const fs::path& path) const
      -> Result<serio::AnyDevice> override
    {
      // Shares writes: an import's resource data files stay open for its
      // appends while the loose writer hashes them. Every other cooked file
      // is replaced by a rename, never written in place.
      auto file
        = serio::MappedFile::Open(path, serio::Sharing::kReadWriteDelete);
      if (!file) {
        return ::oxygen::Err(file.error());
      }
      return ::oxygen::Ok(serio::AnyDevice(std::move(file).value()));
    }

    [[nodiscard]] auto List(const fs::path& path) const
      -> Result<std::vector<content::lc::RootEntry>> override
    {
      try {
        return ::oxygen::Ok(content::lc::ListFolder(path));
      } catch (const fs::filesystem_error& error) {
        return ::oxygen::Err(error.code());
      }
    }

    [[nodiscard]] auto Publish(const fs::path& path)
      -> Result<std::unique_ptr<Publication>> override
    {
      CHECK_RESULT(CreateParent(path));
      auto writer = serio::AtomicFileWriter::Create(path);
      if (!writer) {
        return ::oxygen::Err(writer.error());
      }
      return ::oxygen::Ok(std::unique_ptr<Publication>(
        std::make_unique<AtomicPublication>(std::move(writer).value())));
    }

    [[nodiscard]] auto PublishAsync(serio::IoDriver& driver, fs::path path,
      const std::span<const std::byte> bytes) -> co::Co<Result<void>> override
    {
      if (const auto parent = CreateParent(path); !parent) {
        co_return ::oxygen::Err(parent.error());
      }
      const auto written = co_await serio::AsyncWriteFileAtomically(
        driver, path, bytes, serio::Durability::kBuffered);
      if (!written) {
        co_return ::oxygen::Err(written.error());
      }
      co_return Result<void> {};
    }

    [[nodiscard]] auto OpenData(serio::IoDriver& driver, const fs::path& path)
      -> Result<serio::AnyAsyncDevice> override
    {
      CHECK_RESULT(CreateParent(path));
      auto file = driver.Open(path,
        serio::OpenSpec {
          .access = serio::Access::kReadWrite,
          .creation = serio::Creation::kOpenAlways,
          .sharing = serio::Sharing::kReadWriteDelete,
          .hint = serio::AccessHint::kRandom,
        });
      if (!file) {
        return ::oxygen::Err(file.error());
      }
      return ::oxygen::Ok(serio::AnyAsyncDevice(std::move(file).value()));
    }

    [[nodiscard]] auto Remove(const fs::path& path) -> Result<void> override
    {
      auto ec = std::error_code {};
      fs::remove(base::ToNativePath(path), ec);
      if (ec) {
        return ::oxygen::Err(ec);
      }
      return {};
    }

    [[nodiscard]] auto RemoveEmptyFolder(const fs::path& path)
      -> Result<bool> override
    {
      const auto native = base::ToNativePath(path);
      auto ec = std::error_code {};
      if (!fs::is_directory(native, ec) || ec) {
        return ::oxygen::Ok(false);
      }
      if (!fs::is_empty(native, ec) || ec) {
        return ::oxygen::Ok(false);
      }
      fs::remove(native, ec);
      if (ec) {
        return ::oxygen::Err(ec);
      }
      return ::oxygen::Ok(true);
    }

    [[nodiscard]] auto Move(const fs::path& from, const fs::path& to)
      -> Result<void> override
    {
      // One step that replaces `to`; its errors come from the move itself.
      const auto moved
        = serio::MoveFileOver(base::ToNativePath(from), base::ToNativePath(to));
      if (!moved) {
        return ::oxygen::Err(moved.error());
      }
      return {};
    }

    [[nodiscard]] auto LockExclusive(const fs::path& path)
      -> Result<std::unique_ptr<StorageLock>> override
    {
      CHECK_RESULT(CreateParent(path));
      auto lock = serio::FileLock::Acquire(base::ToNativePath(path),
        serio::FileLockMode::kExclusive,
        serio::FileLockOpenMode::kOpenOrCreate);
      if (!lock) {
        return ::oxygen::Err(lock.error());
      }
      return ::oxygen::Ok(std::unique_ptr<StorageLock>(
        std::make_unique<HeldFileLock>(std::move(lock).value())));
    }

    [[nodiscard]] auto SizeOf(const fs::path& path) const
      -> std::optional<serio::ByteCount> override
    {
      const auto native = base::ToNativePath(path);
      auto ec = std::error_code {};
      if (!fs::is_regular_file(native, ec) || ec) {
        return std::nullopt;
      }
      const auto size = fs::file_size(native, ec);
      if (ec) {
        return std::nullopt;
      }
      return serio::ByteCount { size };
    }

  private:
    static auto CreateParent(const fs::path& path) -> Result<void>
    {
      if (const auto parent = path.parent_path(); !parent.empty()) {
        auto ec = std::error_code {};
        fs::create_directories(base::ToNativePath(parent), ec);
        if (ec) {
          return ::oxygen::Err(ec);
        }
      }
      return {};
    }
  };

} // namespace

auto RealCookerStorage() -> ImportStorage&
{
  // Stateless: one instance serves every caller.
  static auto storage = RealFiles {};
  return storage;
}

auto ReadWholeFile(const CookerStorageReader& storage, const fs::path& path,
  const serio::ByteCount limit) -> Result<std::vector<std::byte>>
{
  auto device = storage.Open(path);
  if (!device) {
    return ::oxygen::Err(device.error());
  }
  const auto size = device->Size();
  if (!size) {
    return ::oxygen::Err(size.error());
  }
  if (*size > limit) {
    return ::oxygen::Err(make_error_code(serio::Error::kTooLarge));
  }
  auto bytes = std::vector<std::byte>(static_cast<size_t>(size->get()));
  CHECK_RESULT(serio::ReadExactlyAt(*device, serio::ByteOffset { 0 }, bytes));
  return ::oxygen::Ok(std::move(bytes));
}

auto PublishWholeFile(CookerStorage& storage, const fs::path& path,
  const std::span<const std::byte> bytes) -> Result<void>
{
  auto publication = storage.Publish(path);
  if (!publication) {
    return ::oxygen::Err(publication.error());
  }
  CHECK_RESULT((*publication)->Write(bytes));
  CHECK_RESULT((*publication)->Flush());
  return (*publication)->Commit();
}

} // namespace oxygen::content::cooked
