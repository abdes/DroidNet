//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <compare>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/api_export.h>

// Forward declarations
namespace oxygen::scene {
class Scene;
class SceneNode;
} // namespace oxygen::scene

namespace oxygen::scene {

//=== Public API Enums ===----------------------------------------------------//

//! Character sets for cross-platform tree visualization
enum class CharacterSet : uint8_t {
  kAscii, //!< Basic ASCII characters: |, -, +, \, /
  kUnicode //!< Unicode box drawing: ├, └, │, ─
};

//! Verbosity levels for scene information display
enum class VerbosityLevel : uint8_t {
  kNone, //!< Structure only (node names)
  kCompact, //!< Key properties abbreviated
  kDetailed //!< Full transform and flag information
};

//! Line ending styles
enum class LineEnding : uint8_t {
  kUnix, //!< Unix-style LF
  kWindows //!< Windows-style CRLF
};

//=== Internal Details ===----------------------------------------------------//

namespace detail {

  //! Configuration options for scene printing
  struct PrintOptions {
    CharacterSet charset = CharacterSet::kUnicode;
    VerbosityLevel verbosity = VerbosityLevel::kCompact;
    LineEnding line_ending = LineEnding::kUnix;
    bool show_transforms = true;
    bool show_flags = true;
    std::ptrdiff_t max_depth = -1; // -1 = unlimited
  };

  //! Core formatting function for scene trees
  OXGN_SCN_API auto FormatSceneTreeCore(const Scene& scene,
    const PrintOptions& options, const std::vector<SceneNode>& roots)
    -> std::vector<std::string>;

} // namespace detail

//=== Line Output Concepts ===------------------------------------------------//

//! Concept for basic line output functionality
template <typename T>
concept LinePrinter = requires(T& printer, std::string_view line) {
  printer.WriteLine(line);
  printer.Flush();
};

//! Concept for buffered line output that can provide its content as a string
template <typename T>
concept BufferedLinePrinter = LinePrinter<T> && requires(const T& printer) {
  { printer.GetBuffer() } -> std::convertible_to<std::string>;
};

//=== Template-based ScenePrettyPrinter ===-----------------------------------//

template <LinePrinter PrinterT> class ScenePrettyPrinter {
public: // Constructor for printers that need no arguments
  ScenePrettyPrinter()
    requires std::is_default_constructible_v<PrinterT>
    : printer_ {}
  {
  }

  // Constructor for printers that need arguments (like StreamPrinter)
  template <typename... Args>
  explicit ScenePrettyPrinter(Args&&... args)
    requires std::is_constructible_v<PrinterT, Args...>
    : printer_(std::forward<Args>(args)...)
  {
  }

  ~ScenePrettyPrinter() = default;

  // Non-copyable - printers may hold references or unique state
  ScenePrettyPrinter(const ScenePrettyPrinter&) = delete;
  auto operator=(const ScenePrettyPrinter&) -> ScenePrettyPrinter& = delete;

  // Movable
  ScenePrettyPrinter(ScenePrettyPrinter&&) = default;
  auto operator=(ScenePrettyPrinter&&) -> ScenePrettyPrinter& = default;

  //=== Configuration methods ===---------------------------------------------//
  //! Set character set for tree drawing
  auto SetCharacterSet(CharacterSet charset) -> ScenePrettyPrinter&
  {
    options_.charset = static_cast<CharacterSet>(charset);
    return *this;
  }

  //! Set verbosity level
  auto SetVerbosity(VerbosityLevel verbosity) -> ScenePrettyPrinter&
  {
    options_.verbosity = static_cast<VerbosityLevel>(verbosity);
    return *this;
  }

  //! Enable/disable transform information display
  auto ShowTransforms(bool show = true) -> ScenePrettyPrinter&
  {
    options_.show_transforms = show;
    return *this;
  }

  //! Enable/disable flag information display
  auto ShowFlags(bool show = true) -> ScenePrettyPrinter&
  {
    options_.show_flags = show;
    return *this;
  }

  //! Set maximum depth (-1 for unlimited)
  auto SetMaxDepth(int max_depth) -> ScenePrettyPrinter&
  {
    options_.max_depth = max_depth;
    return *this;
  }
  //! Set line ending style
  auto SetLineEnding(LineEnding line_ending) -> ScenePrettyPrinter&
  {
    options_.line_ending = static_cast<LineEnding>(line_ending);
    return *this;
  }
  //=== Core printing methods ===---------------------------------------------//
  ////! Print entire scene graph
  void Print(const Scene& scene)
  {
    auto lines
      = detail::FormatSceneTreeCore(scene, options_, scene.GetRootNodes());
    for (const auto& line : lines) {
      printer_.WriteLine(line);
    }
    printer_.Flush();
  }
  //! Print scene subtree from specific root node
  void Print(const Scene& scene, const SceneNode& root)
  {
    if (!root.IsValid()) {
      printer_.WriteLine("Invalid node");
      printer_.Flush();
      return;
    }
    std::vector<SceneNode> roots = { root };
    auto lines = detail::FormatSceneTreeCore(scene, options_, roots);
    for (const auto& line : lines) {
      printer_.WriteLine(line);
    }
    printer_.Flush();
  }

  //! Print scene subtrees from multiple root nodes
  void Print(const Scene& scene, const std::vector<SceneNode>& roots)
  {
    if (roots.empty()) {
      printer_.WriteLine("No roots provided");
      printer_.Flush();
      return;
    }
    auto lines = detail::FormatSceneTreeCore(scene, options_, roots);
    for (const auto& line : lines) {
      printer_.WriteLine(line);
    }
    printer_.Flush();
  }
  //! Get entire scene graph as string - only available for BufferedLinePrinter
  auto ToString(const Scene& scene) const -> std::string
    requires BufferedLinePrinter<PrinterT>
  {
    // Create temporary printer to avoid modifying stored state
    PrinterT temp_printer = printer_;
    auto lines
      = detail::FormatSceneTreeCore(scene, options_, scene.GetRootNodes());
    for (const auto& line : lines) {
      temp_printer.WriteLine(line);
    }
    temp_printer.Flush();
    return temp_printer.GetBuffer();
  }

  //! Get scene subtree as string from specific root node - only available for
  //! BufferedLinePrinter
  auto ToString(const Scene& scene, const SceneNode& root) const -> std::string
    requires BufferedLinePrinter<PrinterT>
  {
    PrinterT temp_printer = printer_;
    if (!root.IsValid()) {
      temp_printer.WriteLine("Invalid node");
      temp_printer.Flush();
      return temp_printer.GetBuffer();
    }
    std::vector<SceneNode> roots = { root };
    auto lines = detail::FormatSceneTreeCore(scene, options_, roots);
    for (const auto& line : lines) {
      temp_printer.WriteLine(line);
    }
    temp_printer.Flush();
    return temp_printer.GetBuffer();
  }

  //! Get scene subtrees as string from multiple root nodes - only available for
  //! BufferedLinePrinter
  auto ToString(const Scene& scene, const std::vector<SceneNode>& roots) const
    -> std::string
    requires BufferedLinePrinter<PrinterT>
  {
    PrinterT temp_printer = printer_;
    if (roots.empty()) {
      temp_printer.WriteLine("No roots provided");
      temp_printer.Flush();
      return temp_printer.GetBuffer();
    }
    auto lines = detail::FormatSceneTreeCore(scene, options_, roots);
    for (const auto& line : lines) {
      temp_printer.WriteLine(line);
    }
    temp_printer.Flush();
    return temp_printer.GetBuffer();
  }

private:
  PrinterT printer_;
  detail::PrintOptions options_;
};

//=== Line Output Implementations ===-----------------------------------------//

//! Output to stdout with compile-time debug control
template <bool DebugOnly = false> struct StdoutPrinter {
  void WriteLine(std::string_view line)
  {
    // This code is active if:
    // 1. StdoutPrinter<false> is used (any build).
    // 2. StdoutPrinter<true> is used in a DEBUG build (NDEBUG is not defined).
    // In a RELEASE build with StdoutPrinter<true>, the specialization is used.
    std::cout << line << '\n';
  }

  void Flush()
  {
    // Similar logic as WriteLine.
    std::cout.flush();
  }
};

//! Output to stderr with compile-time debug control
template <bool DebugOnly = false> struct StderrPrinter {
  void WriteLine(std::string_view line)
  {
    // This code is active if:
    // 1. StderrPrinter<false> is used (any build).
    // 2. StderrPrinter<true> is used in a DEBUG build (NDEBUG is not defined).
    // In a RELEASE build with StderrPrinter<true>, the specialization is used.
    std::cerr << line << '\n';
  }

  void Flush()
  {
    // Similar logic as WriteLine.
    std::cerr.flush();
  }
};

//! Output to arbitrary stream
struct StreamPrinter {
  explicit StreamPrinter(std::ostream& stream)
    : stream_(stream)
  {
  }

  void WriteLine(std::string_view line) const { stream_ << line << '\n'; }

  void Flush() const { stream_.flush(); }

private:
  std::ostream& stream_;
};

//! Output to string buffer
template <bool DebugOnly = false> struct StringPrinter { // Made template
  void WriteLine(std::string_view line)
  {
    // This code is active if:
    // 1. StringPrinter<false> is used (any build).
    // 2. StringPrinter<true> is used in a DEBUG build (NDEBUG is not defined).
    // In a RELEASE build with StringPrinter<true>, the specialization is used.
    buffer_ += line;
    buffer_ += '\n';
  }

  void Flush()
  {
    // No-op for string buffer
  }

  [[nodiscard]] auto GetBuffer() const -> std::string { return buffer_; }

private:
  std::string buffer_;
};

//! Output to logger with compile-time debug control
template <bool DebugOnly = false> struct LoggerPrinter {
  explicit LoggerPrinter(loguru::Verbosity verbosity = loguru::Verbosity_INFO)
    : verbosity_(verbosity)
  {
  }

  void WriteLine(std::string_view line) const
  {
    // This code is active if:
    // 1. LoggerPrinter<false> is used (any build).
    // 2. LoggerPrinter<true> is used in a DEBUG build (NDEBUG is not defined).
    // In a RELEASE build with LoggerPrinter<true>, the specialization is used.

    const auto line_str = std::string(line);

    // Use LOG_F macros - compile-time branch
    switch (verbosity_) {
    case loguru::Verbosity_INFO:
      LOG_F(INFO, "%s", line_str.c_str());
      break;
    case loguru::Verbosity_WARNING:
      LOG_F(WARNING, "%s", line_str.c_str());
      break;
    case loguru::Verbosity_ERROR:
      LOG_F(ERROR, "%s", line_str.c_str());
      break;
    case loguru::Verbosity_1:
      LOG_F(1, "%s", line_str.c_str());
      break;
    case loguru::Verbosity_2:
      LOG_F(2, "%s", line_str.c_str());
      break;
    case loguru::Verbosity_3:
      LOG_F(3, "%s", line_str.c_str());
      break;
    default:
      LOG_F(INFO, "%s", line_str.c_str());
      break;
    }
  }

  void Flush()
  {
    // Logger handles its own flushing
  }

private:
  loguru::Verbosity verbosity_;
};

// Completely compile out debug-only outputs in release builds
#ifdef NDEBUG
template <> struct StdoutPrinter<true> {
  void WriteLine(std::string_view)
  {
    // Completely empty - optimized away
  }
  void Flush()
  {
    // Completely empty - optimized away
  }
};

template <> struct StderrPrinter<true> {
  void WriteLine(std::string_view)
  {
    // Completely empty - optimized away
  }
  void Flush()
  {
    // Completely empty - optimized away
  }
};

template <> struct LoggerPrinter<true> {
  explicit LoggerPrinter(loguru::Verbosity = loguru::Verbosity_INFO) { }
  void WriteLine(std::string_view)
  {
    // Completely empty - optimized away
  }
  void Flush()
  {
    // Completely empty - optimized away
  }
};

template <> struct StringPrinter<true> {
  explicit StringPrinter() { }
  void WriteLine(std::string_view)
  {
    // Completely empty - optimized away
  }
  void Flush()
  {
    // Completely empty - optimized away
  }
  std::string GetBuffer() const
  {
    return ""; // Return empty string
  }
};
#endif

// Verify our implementations satisfy the concepts
static_assert(LinePrinter<StdoutPrinter<false>>);
static_assert(LinePrinter<StderrPrinter<false>>);
static_assert(LinePrinter<StreamPrinter>);
static_assert(LinePrinter<StringPrinter<false>>);
static_assert(LinePrinter<LoggerPrinter<false>>);
static_assert(BufferedLinePrinter<StringPrinter<false>>);

// Also check the DebugOnly = true versions for completeness in debug builds
#ifndef NDEBUG
static_assert(LinePrinter<StdoutPrinter<true>>);
static_assert(LinePrinter<StderrPrinter<true>>);
static_assert(LinePrinter<StringPrinter<true>>);
static_assert(LinePrinter<LoggerPrinter<true>>);
static_assert(BufferedLinePrinter<StringPrinter<true>>);
#endif

//=== Convenient Type Aliases ===---------------------------------------------//

//! Common printer type aliases for convenience
using StringScenePrinter = ScenePrettyPrinter<StringPrinter<false>>;
using StdoutScenePrinter = ScenePrettyPrinter<StdoutPrinter<false>>;
using StderrScenePrinter = ScenePrettyPrinter<StderrPrinter<false>>;
using DebugStdoutScenePrinter = ScenePrettyPrinter<StdoutPrinter<true>>;
using DebugStderrScenePrinter = ScenePrettyPrinter<StderrPrinter<true>>;
using StreamScenePrinter = ScenePrettyPrinter<StreamPrinter>;
using LoggerScenePrinter = ScenePrettyPrinter<LoggerPrinter<false>>;
using DebugLoggerScenePrinter = ScenePrettyPrinter<LoggerPrinter<true>>;

//=== Factory Functions ===---------------------------------------------------//

//! Create a string-based printer
inline auto CreateStringPrinter() -> StringScenePrinter
{
  return StringScenePrinter {};
}

//! Create a stream-based printer
inline auto CreateStreamPrinter(std::ostream& stream) -> StreamScenePrinter
{
  return StreamScenePrinter { stream };
}

//! Create a logger-based printer
template <bool DebugOnly = false>
inline auto CreateLoggerPrinter(
  loguru::Verbosity verbosity = loguru::Verbosity_INFO)
{
  if constexpr (DebugOnly) {
    return ScenePrettyPrinter<LoggerPrinter<true>> { verbosity };
  } else {
    return ScenePrettyPrinter<LoggerPrinter<false>> { verbosity };
  }
}

//! Create a stdout-based printer
template <bool DebugOnly = false> inline auto CreateStdoutPrinter()
{
  if constexpr (DebugOnly) {
    return ScenePrettyPrinter<StdoutPrinter<true>> {};
  } else {
    return ScenePrettyPrinter<StdoutPrinter<false>> {};
  }
}

//! Create a stderr-based printer
template <bool DebugOnly = false> inline auto CreateStderrPrinter()
{
  if constexpr (DebugOnly) {
    return ScenePrettyPrinter<StderrPrinter<true>> {};
  } else {
    return ScenePrettyPrinter<StderrPrinter<false>> {};
  }
}

//=== Helper functions for common patterns ===--------------------------------//

//! Convenience function for runtime debug logging
inline void LogSceneGraph(const Scene& scene, bool debug_mode = false,
  loguru::Verbosity verbosity = loguru::Verbosity_INFO)
{
  if (debug_mode) {
    auto printer = CreateLoggerPrinter<true>(verbosity);
    printer.ShowTransforms(true).Print(scene);
  } else {
    auto printer = CreateLoggerPrinter<false>(verbosity);
    printer.ShowTransforms(true).Print(scene);
  }
}

} // namespace oxygen::scene
