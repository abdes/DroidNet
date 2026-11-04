//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>

namespace oxygen::content::import::tool::test {

//! A message writer that keeps what a command prints, for tests to read.
class CapturingWriter final : public IMessageWriter {
public:
  auto Error(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Warning(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Info(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Report(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  auto Progress(const std::string_view message) -> bool override
  {
    messages_.emplace_back(message);
    return true;
  }

  [[nodiscard]] auto JoinedMessages() const -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& msg : messages_) {
      out << msg << "\n";
    }
    return out.str();
  }

private:
  std::vector<std::string> messages_;
};

} // namespace oxygen::content::import::tool::test
