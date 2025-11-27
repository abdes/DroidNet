//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/RendererConfig.h>

// Std / interop
#ifdef _WIN32
#  include <WinSock2.h> // include before any header that might include <windows.h>
#endif
#include <msclr/marshal_cppstd.h>
#include <string>
#include <optional>

// The purpose of this header is to expose managed (C++/CLI) mirror types for
// the native Oxygen engine configuration structures so they can be consumed
// naturally from .NET (e.g., the Editor) while retaining a clear and explicit
// conversion boundary.  Each managed type:
//  - Mirrors the fields of its native counterpart using .NET friendly types
//  - Provides static FromNative(...) and instance ToNative() helpers
//  - Uses null (or Nullable<T>) to represent std::optional fields
//  - Represents time durations as System::TimeSpan for ergonomic .NET usage
//
// Guidelines:
//  - Keep these DTO-like; do not embed engine logic here.
//  - Any future native field additions must be reflected here with matching
//    conversion code to avoid silent configuration loss.
//  - Favor explicit conversions (no implicit operators) to make crossing the
//    boundary obvious during code reviews.
//
// Thread-safety: These managed objects are plain data holders and are not
// thread-safe; construct them on the UI / configuration thread then convert
// once before passing the native object into engine systems.
//
// Naming: We append the suffix 'Managed' to avoid collisions with the native
// names and to make intent clear at the call-site.

namespace Oxygen::Editor::EngineInterface {

  // Forward declarations of native types (already included but for clarity)
  namespace native = ::oxygen;

  // Helper namespace for small conversion utilities.
  namespace detail {
    inline std::string ToStdString(System::String^ s) {
      return s ? msclr::interop::marshal_as<std::string>(s) : std::string();
    }
    inline System::String^ ToManagedString(std::string const& s) {
      return gcnew System::String(s.c_str());
    }
    // TimeSpan ticks are 100ns units.
    inline System::TimeSpan NsToTimeSpan(std::chrono::nanoseconds ns) {
      // Guard against negative (should not occur for our config values)
      auto ticks = ns.count() / 100; // integer division
      return System::TimeSpan::FromTicks(ticks);
    }
    inline std::chrono::nanoseconds TimeSpanToNs(System::TimeSpan ts) {
      return std::chrono::nanoseconds(ts.Ticks * 100); // 100ns per tick
    }
    inline System::TimeSpan MicrosecondsToTimeSpan(std::chrono::microseconds us) {
      // microsecond -> ticks (100ns): 1us = 10 ticks
      auto ticks = us.count() * 10;
      return System::TimeSpan::FromTicks(ticks);
    }
    inline std::chrono::microseconds TimeSpanToMicroseconds(System::TimeSpan ts) {
      // ticks (100ns) -> microseconds: divide by 10
      return std::chrono::microseconds(ts.Ticks / 10);
    }
  } // namespace detail

  // RendererConfig ---------------------------------------------------------
  public ref class RendererConfigManaged sealed {
  public:
    System::String^ UploadQueueKey; // required

    RendererConfigManaged() : UploadQueueKey(gcnew System::String("")) {}

    static RendererConfigManaged^ FromNative(native::RendererConfig const& n) {
      auto m = gcnew RendererConfigManaged();
      m->UploadQueueKey = detail::ToManagedString(n.upload_queue_key);
      return m;
    }

    native::RendererConfig ToNative() {
      native::RendererConfig n;
      n.upload_queue_key = detail::ToStdString(UploadQueueKey);
      return n;
    }
  };

  // LoggingConfig ----------------------------------------------------------
  public
  ref class LoggingConfig sealed {
  public:
    LoggingConfig() {
      // default verbosity (OFF)
      Verbosity = -9;
      IsColored = false;
      ModuleOverrides = gcnew System::String("");
    }

    property int Verbosity;
    property bool IsColored;
    property System::String^ ModuleOverrides;

    // Minimum and maximum allowed verbosity values (loguru constants).
    static property int MinVerbosity {
      int get() { return static_cast<int>(::loguru::Verbosity_OFF); }
    }

    static property int MaxVerbosity {
      int get() { return static_cast<int>(::loguru::Verbosity_MAX); }
    }
  };

  // GraphicsConfig ---------------------------------------------------------
  public ref class GraphicsConfigManaged sealed {
  public:
    bool EnableDebug;
    bool EnableValidation;
    System::String^ PreferredCardName; // null => not specified
    System::Nullable<long long> PreferredCardDeviceId; // null => not specified
    bool Headless;
    bool EnableImGui;
    bool EnableVSync;
    System::String^ Extra; // JSON string

    GraphicsConfigManaged()
      : EnableDebug(false)
      , EnableValidation(false)
      , PreferredCardName(nullptr)
      , PreferredCardDeviceId()
      , Headless(false)
      , EnableImGui(false)
      , EnableVSync(true)
      , Extra(gcnew System::String("{}")) {
    }

    static GraphicsConfigManaged^ FromNative(native::GraphicsConfig const& n) {
      auto m = gcnew GraphicsConfigManaged();
      m->EnableDebug = n.enable_debug;
      m->EnableValidation = n.enable_validation;
      if (n.preferred_card_name.has_value()) {
        m->PreferredCardName = detail::ToManagedString(*n.preferred_card_name);
      }
      if (n.preferred_card_device_id.has_value()) {
        m->PreferredCardDeviceId = System::Nullable<long long>(*n.preferred_card_device_id);
      }
      m->Headless = n.headless;
      m->EnableImGui = n.enable_imgui;
      m->EnableVSync = n.enable_vsync;
      m->Extra = detail::ToManagedString(n.extra);
      return m;
    }

    native::GraphicsConfig ToNative() {
      native::GraphicsConfig n;
      n.enable_debug = EnableDebug;
      n.enable_validation = EnableValidation;
      if (!System::String::IsNullOrEmpty(PreferredCardName)) {
        n.preferred_card_name = detail::ToStdString(PreferredCardName);
      }
      if (PreferredCardDeviceId.HasValue) {
        n.preferred_card_device_id = static_cast<native::DeviceId>(PreferredCardDeviceId.Value);
      }
      n.headless = Headless;
      n.enable_imgui = EnableImGui;
      n.enable_vsync = EnableVSync;
      n.extra = Extra != nullptr ? detail::ToStdString(Extra) : std::string("{}");
      return n;
    }
  };

  // PlatformConfig ---------------------------------------------------------
  public ref class PlatformConfigManaged sealed {
  public:
    bool Headless;
    System::UInt32 ThreadPoolSize; // 0 = none

    PlatformConfigManaged() : Headless(false), ThreadPoolSize(0) {}

    static PlatformConfigManaged^ FromNative(native::PlatformConfig const& n) {
      auto m = gcnew PlatformConfigManaged();
      m->Headless = n.headless;
      m->ThreadPoolSize = n.thread_pool_size;
      return m;
    }

    native::PlatformConfig ToNative() {
      native::PlatformConfig n;
      n.headless = Headless;
      n.thread_pool_size = ThreadPoolSize;
      return n;
    }
  };

  // TimingConfig -----------------------------------------------------------
  public ref class TimingConfigManaged sealed {
  public:
    System::TimeSpan FixedDelta;            // canonical (nanoseconds)
    System::TimeSpan MaxAccumulator;        // canonical (nanoseconds)
    System::UInt32  MaxSubsteps;            // max iterations / frame
    System::TimeSpan PacingSafetyMargin;    // microseconds

    TimingConfigManaged() :
      FixedDelta(System::TimeSpan::FromTicks(0)),
      MaxAccumulator(System::TimeSpan::FromTicks(0)),
      MaxSubsteps(0),
      PacingSafetyMargin(System::TimeSpan::FromTicks(0)) {
    }

    static TimingConfigManaged^ FromNative(native::TimingConfig const& n) {
      auto m = gcnew TimingConfigManaged();
      // NamedType wraps chrono::nanoseconds
      m->FixedDelta = detail::NsToTimeSpan(n.fixed_delta.get());
      m->MaxAccumulator = detail::NsToTimeSpan(n.max_accumulator.get());
      m->MaxSubsteps = n.max_substeps;
      m->PacingSafetyMargin = detail::MicrosecondsToTimeSpan(n.pacing_safety_margin);
      return m;
    }

    native::TimingConfig ToNative() {
      native::TimingConfig n;
      n.fixed_delta = native::time::CanonicalDuration{ detail::TimeSpanToNs(FixedDelta) };
      n.max_accumulator = native::time::CanonicalDuration{ detail::TimeSpanToNs(MaxAccumulator) };
      n.max_substeps = MaxSubsteps;
      n.pacing_safety_margin = detail::TimeSpanToMicroseconds(PacingSafetyMargin);
      return n;
    }
  };

  // EngineConfig -----------------------------------------------------------
  public ref class EngineConfig sealed {
  public:
    // Application sub-structure
    System::String^ ApplicationName;
    System::UInt32  ApplicationVersion;

    System::UInt32 TargetFps; // 0 = uncapped
    System::UInt32 FrameCount; // 0 = unlimited / run until exit

    GraphicsConfigManaged^ Graphics; // required logically; allow null -> default
    TimingConfigManaged^ Timing;   // required logically; allow null -> default

    EngineConfig()
      : ApplicationName(gcnew System::String("")),
      ApplicationVersion(0),
      TargetFps(0),
      FrameCount(0),
      Graphics(nullptr),
      Timing(nullptr) {
    }

    static EngineConfig^ FromNative(native::EngineConfig const& n) {
      auto m = gcnew EngineConfig();
      m->ApplicationName = detail::ToManagedString(n.application.name);
      m->ApplicationVersion = n.application.version;
      m->TargetFps = n.target_fps;
      m->FrameCount = n.frame_count;
      m->Graphics = GraphicsConfigManaged::FromNative(n.graphics);
      m->Timing = TimingConfigManaged::FromNative(n.timing);
      return m;
    }

    native::EngineConfig ToNative() {
      native::EngineConfig n; // value-init uses defaults from structs
      n.application.name = detail::ToStdString(ApplicationName);
      n.application.version = ApplicationVersion;
      n.target_fps = TargetFps;
      n.frame_count = FrameCount;
      if (Graphics != nullptr) {
        n.graphics = Graphics->ToNative();
      }
      else {
        n.graphics = native::GraphicsConfig{}; // default
      }
      if (Timing != nullptr) {
        n.timing = Timing->ToNative();
      }
      else {
        n.timing = native::TimingConfig{}; // default
      }
      return n;
    }

    /// <summary>
    /// Maximum allowed target FPS as defined by the native engine config.
    /// This exposes the native EngineConfig::kMaxTargetFps to managed callers.
    /// </summary>
    static property System::UInt32 MaxTargetFps {
      System::UInt32 get() {
        return native::EngineConfig::kMaxTargetFps;
      }
    }
  };

  // Convenience aggregating helper (optional future extension point)
  public ref class ConfigFactory abstract sealed {
  public:
    static EngineConfig^ CreateDefaultEngineConfig() {
      native::EngineConfig native_default; // default constructed
      return EngineConfig::FromNative(native_default);
    }
  };

} // namespace Oxygen::Editor::EngineInterface
