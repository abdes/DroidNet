//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/Detail/GuardedCopy.h>
#include <Oxygen/Serio/MappedFile.h>

namespace oxygen::serio {

namespace {

  //! The jump target of the guarded copy running on this thread, if any.
  thread_local sigjmp_buf* t_guard = nullptr;

  struct sigaction g_previous_bus_action {};

  auto OnBusError(const int signal, siginfo_t* info, void* context) -> void
  {
    if (t_guard != nullptr) {
      siglongjmp(*t_guard, 1);
    }
    // Not a guarded copy: hand the fault to whoever handled it before.
    if ((g_previous_bus_action.sa_flags & SA_SIGINFO) != 0
      && g_previous_bus_action.sa_sigaction != nullptr) {
      g_previous_bus_action.sa_sigaction(signal, info, context);
      return;
    }
    if (g_previous_bus_action.sa_handler != SIG_IGN
      && g_previous_bus_action.sa_handler != SIG_DFL) {
      g_previous_bus_action.sa_handler(signal);
      return;
    }
    ::signal(SIGBUS, SIG_DFL);
    ::raise(SIGBUS);
  }

  auto InstallBusHandler() noexcept -> void
  {
    static std::once_flag once;
    std::call_once(once, [] {
      struct sigaction action {};
      action.sa_sigaction = OnBusError;
      action.sa_flags = SA_SIGINFO;
      sigemptyset(&action.sa_mask);
      ::sigaction(SIGBUS, &action, &g_previous_bus_action);
    });
  }

} // namespace

namespace detail {

  auto GuardedCopy(std::byte* destination, const std::byte* source,
    const size_t size) noexcept -> bool
  {
    InstallBusHandler();
    sigjmp_buf jump;
    if (sigsetjmp(jump, 1) != 0) {
      t_guard = nullptr;
      return false;
    }
    t_guard = &jump;
    std::memcpy(destination, source, size);
    t_guard = nullptr;
    return true;
  }

} // namespace detail

auto MappedFile::Open(const std::filesystem::path& path) noexcept
  -> Result<MappedFile>
{
  try {
    auto logical = path;
    const auto native = base::ToNativePath(path);
    const auto descriptor = ::open(native.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
      return ::oxygen::Err(base::LastSystemError());
    }
    // The mapping keeps the file alive; the descriptor closes once mapped.
    const auto close
      = ScopeGuard([descriptor]() noexcept { ::close(descriptor); });

    struct stat info {};
    if (::fstat(descriptor, &info) != 0) {
      return ::oxygen::Err(base::LastSystemError());
    }
    const auto size = static_cast<uint64_t>(info.st_size);
    if (size > (std::numeric_limits<size_t>::max)()) {
      return ::oxygen::Err(std::make_error_code(std::errc::value_too_large));
    }
    if (size == 0) {
      return ::oxygen::Ok(MappedFile(nullptr, 0, std::move(logical)));
    }
    auto* const view = ::mmap(nullptr, static_cast<size_t>(size), PROT_READ,
      MAP_PRIVATE, descriptor, 0);
    if (view == MAP_FAILED) {
      return ::oxygen::Err(base::LastSystemError());
    }
    return ::oxygen::Ok(MappedFile(static_cast<std::byte*>(view),
      static_cast<size_t>(size), std::move(logical)));
  } catch (const std::bad_alloc&) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  } catch (const std::filesystem::filesystem_error& error) {
    return ::oxygen::Err(error.code());
  }
}

auto MappedFile::Close() noexcept -> void
{
  if (view_ != nullptr) {
    ::munmap(view_, size_);
    view_ = nullptr;
    size_ = 0;
  }
}

auto MappedFile::Prefetch(const ByteRange range) const noexcept -> void
{
  if (range.offset.get() >= size_ || range.count.get() == 0) {
    return;
  }
  const auto page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
  const auto offset = static_cast<size_t>(range.offset.get());
  const auto count = static_cast<size_t>(
    (std::min)(range.count.get(), uint64_t { size_ - offset }));
  const auto start = offset - (offset % page);
  ::madvise(
    Pages().subspan(start).data(), count + (offset - start), MADV_WILLNEED);
}

} // namespace oxygen::serio
