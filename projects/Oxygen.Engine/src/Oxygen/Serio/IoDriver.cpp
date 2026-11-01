//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <new>
#include <system_error>
#include <utility>
#include <variant>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Detail/InlineBackend.h>
#include <Oxygen/Serio/Detail/IoBackend.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Platform/Windows/IocpBackend.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace detail {
  auto Submit(IoRequest& request) noexcept -> bool
  {
    return VisitBackend(request.target.driver->backend,
      [&request](
        auto& backend) noexcept -> bool { return backend.Submit(request); });
  }

  auto Cancel(IoRequest& request) noexcept -> void
  {
    VisitBackend(request.target.driver->backend,
      [&request](auto& backend) noexcept -> void { backend.Cancel(request); });
  }
} // namespace detail

auto IoDriver::Create(asio::io_context& loop) noexcept -> Result<IoDriver>
{
#ifdef OXYGEN_WINDOWS
  auto backend = detail::Backend { detail::IocpBackend(loop) };
#else
  auto backend = detail::Backend { detail::UringBackend(loop) };
#endif
  auto state = std::unique_ptr<detail::DriverState>(
    new (std::nothrow) detail::DriverState(backend));
  if (!state) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
  return ::oxygen::Ok(IoDriver(std::move(state)));
}

auto IoDriver::CreateInline() noexcept -> Result<IoDriver>
{
  auto state = std::unique_ptr<detail::DriverState>(new (std::nothrow)
      detail::DriverState(detail::Backend { detail::InlineBackend {} }));
  if (!state) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
  return ::oxygen::Ok(IoDriver(std::move(state)));
}

auto IoDriver::Inline() const noexcept -> bool
{
  return std::holds_alternative<detail::InlineBackend>(state_->backend);
}

IoDriver::IoDriver(std::unique_ptr<detail::DriverState> state) noexcept
  : state_(std::move(state))
{
}

IoDriver::~IoDriver() noexcept
{
  DCHECK_F(!state_ || state_->open_files.load() == 0,
    "an IoDriver is destroyed with {} files open on it",
    state_->open_files.load());
}

IoDriver::IoDriver(IoDriver&& other) noexcept = default;

auto IoDriver::operator=(IoDriver&& other) noexcept -> IoDriver& = default;

auto IoDriver::Open(const std::filesystem::path& path,
  const OpenSpec& spec) noexcept -> Result<AsyncFile>
{
  // An inline driver's files are ordinary ones: nothing completes on a port.
  auto file = Inline() ? NativeFile::Open(path, spec)
                       : NativeFile::OpenForDriver(path, spec);
  if (!file) {
    return ::oxygen::Err(file.error());
  }
  auto slot = detail::VisitBackend(state_->backend,
    [handle = file->Handle()](auto& backend) noexcept
      -> Result<detail::BackendFileSlot> { return backend.Register(handle); });
  if (!slot) {
    return ::oxygen::Err(slot.error());
  }
  return ::oxygen::Ok(AsyncFile(std::move(*file), *state_, *slot));
}

auto IoDriver::AllocateBuffer(const ByteCount size) noexcept
  -> Result<ByteBuffer>
{
  return detail::VisitBackend(
    state_->backend, [size](auto& backend) noexcept -> Result<ByteBuffer> {
      return backend.AllocateBuffer(size);
    });
}

AsyncFile::AsyncFile(NativeFile file, detail::DriverState& driver,
  const detail::BackendFileSlot slot) noexcept
  : file_(std::move(file))
  , driver_(&driver)
  , slot_(slot)
{
  driver_->open_files.fetch_add(1);
}

AsyncFile::~AsyncFile() noexcept { Close(); }

AsyncFile::AsyncFile(AsyncFile&& other) noexcept
  : file_(std::move(other.file_))
  , driver_(std::exchange(other.driver_, nullptr))
  , slot_(std::exchange(other.slot_, {}))
{
}

auto AsyncFile::operator=(AsyncFile&& other) noexcept -> AsyncFile&
{
  if (this != &other) {
    Close();
    file_ = std::move(other.file_);
    driver_ = std::exchange(other.driver_, nullptr);
    slot_ = std::exchange(other.slot_, {});
  }
  return *this;
}

auto AsyncFile::Close() noexcept -> void
{
  if (driver_ == nullptr) {
    return;
  }
  // The backend releases its registration before the file's handle closes.
  detail::VisitBackend(driver_->backend,
    [this](auto& backend) noexcept -> void { backend.Unregister(slot_); });
  driver_->open_files.fetch_sub(1);
  driver_ = nullptr;
}

} // namespace oxygen::serio
