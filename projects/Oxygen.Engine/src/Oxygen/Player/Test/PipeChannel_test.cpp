//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h>

#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <Oxygen/Player/PipeChannel.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace std::chrono_literals;
using oxygen::player::PipeChannel;

constexpr auto kTimeout = 5s;

//! The launcher's end: a duplex byte pipe server.
class PipeServer {
public:
  explicit PipeServer(const std::string& name)
    : handle_(CreateNamedPipeA(("\\\\.\\pipe\\" + name).c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
        4096, 4096, 0, nullptr))
  {
    if (handle_ == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("CreateNamedPipe failed");
    }
  }
  ~PipeServer() { Close(); }

  PipeServer(const PipeServer&) = delete;
  auto operator=(const PipeServer&) -> PipeServer& = delete;
  PipeServer(PipeServer&&) = delete;
  auto operator=(PipeServer&&) -> PipeServer& = delete;

  auto Accept() const -> void
  {
    const auto ok = ConnectNamedPipe(handle_, nullptr);
    ASSERT_TRUE(ok != FALSE || GetLastError() == ERROR_PIPE_CONNECTED);
  }

  auto Write(const std::string& text) const -> void
  {
    auto written = DWORD { 0U };
    ASSERT_NE(WriteFile(handle_, text.data(), static_cast<DWORD>(text.size()),
                &written, nullptr),
      FALSE);
  }

  //! Reads until `count` lines arrived.
  auto ReadLines(const std::size_t count) const -> std::vector<std::string>
  {
    auto lines = std::vector<std::string> {};
    auto pending = std::string {};
    auto chunk = std::array<char, 256> {};
    while (lines.size() < count) {
      auto read = DWORD { 0U };
      if (ReadFile(handle_, chunk.data(), static_cast<DWORD>(chunk.size()),
            &read, nullptr)
          == FALSE
        || read == 0U) {
        break;
      }
      pending.append(chunk.data(), read);
      for (auto end = pending.find('\n'); end != std::string::npos;
        end = pending.find('\n')) {
        lines.push_back(pending.substr(0U, end));
        pending.erase(0U, end + 1U);
      }
    }
    return lines;
  }

  auto Close() -> void
  {
    if (handle_ != INVALID_HANDLE_VALUE) {
      DisconnectNamedPipe(handle_);
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
  }

private:
  HANDLE handle_;
};

auto UniquePipeName(const char* test) -> std::string
{
  return "oxygen-player-test-" + std::to_string(GetCurrentProcessId()) + "-"
    + test;
}

//! Polls until `count` requests arrived or the timeout expires.
auto WaitForRequests(PipeChannel& channel, const std::size_t count)
  -> std::vector<std::string>
{
  auto requests = std::vector<std::string> {};
  const auto deadline = std::chrono::steady_clock::now() + kTimeout;
  while (
    requests.size() < count && std::chrono::steady_clock::now() < deadline) {
    for (auto& line : channel.TakeRequests()) {
      requests.push_back(std::move(line));
    }
    std::this_thread::sleep_for(5ms);
  }
  return requests;
}

NOLINT_TEST(PipeChannelTest, SendsLinesAndReceivesRequests)
{
  const auto name = UniquePipeName("exchange");
  auto server = PipeServer(name);
  auto channel = PipeChannel::Connect(name, kTimeout);
  server.Accept();

  channel->Send(R"({"event":"lifecycle","state":"starting"})");
  channel->Send(R"({"event":"lifecycle","state":"stopped"})");
  const auto lines = server.ReadLines(2U);
  ASSERT_EQ(lines.size(), 2U);
  EXPECT_EQ(lines[0], R"({"event":"lifecycle","state":"starting"})");
  EXPECT_EQ(lines[1], R"({"event":"lifecycle","state":"stopped"})");

  // A request split across writes, and two in one write, with CRLF.
  server.Write(R"({"request":)");
  server.Write(
    "\"pause\"}\r\n{\"request\":\"resume\"}\n{\"request\":\"stop\"}\n");
  const auto requests = WaitForRequests(*channel, 3U);
  ASSERT_EQ(requests.size(), 3U);
  EXPECT_EQ(requests[0], R"({"request":"pause"})");
  EXPECT_EQ(requests[1], R"({"request":"resume"})");
  EXPECT_EQ(requests[2], R"({"request":"stop"})");
  EXPECT_FALSE(channel->IsClosed());
}

NOLINT_TEST(PipeChannelTest, ReportsTheLauncherClosingThePipe)
{
  const auto name = UniquePipeName("close");
  auto server = PipeServer(name);
  auto channel = PipeChannel::Connect(name, kTimeout);
  server.Accept();

  server.Close();

  const auto deadline = std::chrono::steady_clock::now() + kTimeout;
  while (!channel->IsClosed() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  EXPECT_TRUE(channel->IsClosed());
  // Sends after the close are dropped.
  channel->Send("ignored");
}

NOLINT_TEST(PipeChannelTest, ConnectFailsWithoutALauncher)
{
  EXPECT_THROW(
    static_cast<void>(PipeChannel::Connect(UniquePipeName("absent"), 100ms)),
    std::runtime_error);
}

} // namespace
