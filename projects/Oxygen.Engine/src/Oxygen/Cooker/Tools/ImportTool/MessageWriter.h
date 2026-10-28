//===----------------------------------------------------------------------===//
// Simple console message writer for ImportTool.
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>

namespace oxygen::content::import::tool {

class IMessageWriter {
public:
  IMessageWriter() = default;
  virtual ~IMessageWriter() = default;
  OXYGEN_MAKE_NON_COPYABLE(IMessageWriter)
  OXYGEN_MAKE_NON_MOVABLE(IMessageWriter)

  // Return true if the message was emitted.
  virtual auto Error(std::string_view message) -> bool = 0;
  virtual auto Warning(std::string_view message) -> bool = 0;
  virtual auto Info(std::string_view message) -> bool = 0;
  virtual auto Report(std::string_view message) -> bool = 0;
  virtual auto Progress(std::string_view message) -> bool = 0;
};

} // namespace oxygen::content::import::tool
