//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <sstream>
#include <string>

namespace oxygen::testing {

//! Scoped capture of what code writes to a standard stream object.
/*! Swaps the stream buffer of `stream` for an in-memory one on construction
    and restores the original on destruction. It sees writes made through the
    `std::ostream` object (for example `std::cout` or `std::cerr`), not writes
    made directly to the C `stdout`/`stderr` file handles.

    Usage example:
      const ScopedStreamCapture captured_stdout { std::cout };
      RunTool();
      EXPECT_TRUE(captured_stdout.Text().empty());
 */
class ScopedStreamCapture {
public:
  explicit ScopedStreamCapture(std::ostream& stream)
    : stream_(stream)
    , saved_(stream.rdbuf(buffer_.rdbuf()))
  {
  }

  ScopedStreamCapture(const ScopedStreamCapture&) = delete;
  auto operator=(const ScopedStreamCapture&) -> ScopedStreamCapture& = delete;
  ScopedStreamCapture(ScopedStreamCapture&&) = delete;
  auto operator=(ScopedStreamCapture&&) -> ScopedStreamCapture& = delete;

  ~ScopedStreamCapture() { stream_.rdbuf(saved_); }

  //! Text written to the stream since construction.
  [[nodiscard]] auto Text() const -> std::string { return buffer_.str(); }

private:
  std::ostream& stream_;
  std::ostringstream buffer_;
  std::streambuf* saved_;
};

} // namespace oxygen::testing
