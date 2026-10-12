//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <errhandlingapi.h>
#include <fileapi.h>
#include <fmt/format.h>
#include <handleapi.h>
#include <ioapiset.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <namedpipeapi.h>
#include <synchapi.h>
#include <winbase.h>
#include <winerror.h>
#include <winnt.h>

#include <Oxygen/Player/PipeChannel.h>

namespace oxygen::player {

namespace {

  constexpr std::size_t kReadChunkBytes = 4096U;

  //! Closes a handle owned by the channel.
  struct HandleCloser {
    auto operator()(void* handle) const noexcept -> void
    {
      if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
      }
    }
  };
  using UniqueHandle = std::unique_ptr<void, HandleCloser>;

  auto MakeEvent() -> UniqueHandle
  {
    auto event = UniqueHandle { CreateEventW(nullptr, TRUE, FALSE, nullptr) };
    if (!event) {
      throw std::runtime_error(
        fmt::format("CreateEvent failed ({})", GetLastError()));
    }
    return event;
  }

  auto ToWide(const std::string_view text) -> std::wstring
  {
    return { text.begin(), text.end() };
  }

} // namespace

// The pipe is opened for overlapped I/O: with a synchronous handle a pending
// read would hold every write on the same handle until a request arrives.
struct PipeChannel::Handles {
  UniqueHandle pipe;
  //! Signalled to end the reader.
  UniqueHandle stop;
  UniqueHandle read_done;
  UniqueHandle write_done;
};

auto PipeChannel::Connect(const std::string_view name,
  const std::chrono::milliseconds timeout) -> std::unique_ptr<PipeChannel>
{
  const auto path = L"\\\\.\\pipe\\" + ToWide(name);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    auto pipe
      = UniqueHandle { CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr) };
    const auto error = GetLastError();
    if (pipe.get() != INVALID_HANDLE_VALUE) {
      auto handles = std::make_unique<Handles>();
      handles->pipe = std::move(pipe);
      handles->stop = MakeEvent();
      handles->read_done = MakeEvent();
      handles->write_done = MakeEvent();
      return std::unique_ptr<PipeChannel>(new PipeChannel(std::move(handles)));
    }
    const auto remaining
      = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (error != ERROR_PIPE_BUSY || remaining.count() <= 0
      || WaitNamedPipeW(path.c_str(), static_cast<DWORD>(remaining.count()))
        == FALSE) {
      throw std::runtime_error(
        fmt::format("cannot connect to pipe '{}' (error {})", name, error));
    }
  }
}

PipeChannel::PipeChannel(std::unique_ptr<Handles> handles)
  : handles_(std::move(handles))
{
  reader_ = std::thread([this] -> void { ReadLoop(); });
}

PipeChannel::~PipeChannel()
{
  SetEvent(handles_->stop.get());
  if (reader_.joinable()) {
    reader_.join();
  }
  {
    // Lines already written reach the launcher before the pipe closes.
    const auto lock = std::scoped_lock(write_mutex_);
    FlushFileBuffers(handles_->pipe.get());
  }
}

auto PipeChannel::Send(const std::string_view line) noexcept -> void
{
  if (IsClosed()) {
    return;
  }
  try {
    auto buffer = std::string(line);
    buffer.push_back('\n');
    const auto lock = std::scoped_lock(write_mutex_);
    auto overlapped = OVERLAPPED {};
    overlapped.hEvent = handles_->write_done.get();
    ResetEvent(overlapped.hEvent);
    auto written = DWORD { 0U };
    auto ok = WriteFile(handles_->pipe.get(), buffer.data(),
      static_cast<DWORD>(buffer.size()), nullptr, &overlapped);
    if (ok == FALSE && GetLastError() == ERROR_IO_PENDING) {
      ok = GetOverlappedResult(
        handles_->pipe.get(), &overlapped, &written, TRUE);
    }
    if (ok == FALSE) {
      closed_.store(true, std::memory_order_release);
    }
  } catch (...) {
    // The line could not be copied or the lock taken: drop it.
    return;
  }
}

auto PipeChannel::TakeRequests() -> std::vector<std::string>
{
  const auto lock = std::scoped_lock(requests_mutex_);
  return std::exchange(requests_, {});
}

auto PipeChannel::ReadLoop() noexcept -> void
{
  auto pending = std::string {};
  auto chunk = std::array<char, kReadChunkBytes> {};
  const auto events
    = std::array<HANDLE, 2> { handles_->read_done.get(), handles_->stop.get() };
  for (;;) {
    auto overlapped = OVERLAPPED {};
    overlapped.hEvent = handles_->read_done.get();
    ResetEvent(overlapped.hEvent);
    auto read = DWORD { 0U };
    auto ok = ReadFile(handles_->pipe.get(), chunk.data(),
      static_cast<DWORD>(chunk.size()), nullptr, &overlapped);
    if (ok == FALSE && GetLastError() == ERROR_IO_PENDING) {
      const auto woken = WaitForMultipleObjects(
        static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE);
      if (woken != WAIT_OBJECT_0) {
        CancelIoEx(handles_->pipe.get(), &overlapped);
        GetOverlappedResult(handles_->pipe.get(), &overlapped, &read, TRUE);
        return;
      }
      ok = GetOverlappedResult(handles_->pipe.get(), &overlapped, &read, FALSE);
    } else if (ok != FALSE) {
      ok = GetOverlappedResult(handles_->pipe.get(), &overlapped, &read, FALSE);
    }
    if (ok == FALSE || read == 0U) {
      // The launcher closed its end.
      closed_.store(true, std::memory_order_release);
      return;
    }
    try {
      pending.append(chunk.data(), read);
      auto lines = std::vector<std::string> {};
      for (auto end = pending.find('\n'); end != std::string::npos;
        end = pending.find('\n')) {
        auto line = pending.substr(0U, end);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (!line.empty()) {
          lines.push_back(std::move(line));
        }
        pending.erase(0U, end + 1U);
      }
      if (!lines.empty()) {
        const auto lock = std::scoped_lock(requests_mutex_);
        for (auto& line : lines) {
          requests_.push_back(std::move(line));
        }
      }
    } catch (...) {
      closed_.store(true, std::memory_order_release);
      return;
    }
  }
}

} // namespace oxygen::player
