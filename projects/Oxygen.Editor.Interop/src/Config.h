//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/EditorInterface/Api.h>

// Std / interop
#ifdef _WIN32
#include <WinSock2.h> // include before any header that might include <windows.h>
#endif
#include <msclr/marshal_cppstd.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

namespace Oxygen::Interop {

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
    inline System::TimeSpan MillisecondsToTimeSpan(std::chrono::milliseconds ms) {
      return System::TimeSpan::FromMilliseconds(ms.count());
    }
    inline std::chrono::milliseconds TimeSpanToMilliseconds(System::TimeSpan ts) {
      return std::chrono::milliseconds(
        static_cast<long long>(ts.TotalMilliseconds));
    }
    inline auto ToPathVector(array<System::String^>^ paths)
      -> std::vector<std::filesystem::path> {
      std::vector<std::filesystem::path> result;
      if (paths == nullptr) {
        return result;
      }
      result.reserve(paths->Length);
      for each (System::String ^ path in paths) {
        if (!System::String::IsNullOrWhiteSpace(path)) {
          result.emplace_back(ToStdString(path));
        }
      }
      return result;
    }
    inline auto ToManagedStringArray(
      std::vector<std::filesystem::path> const& paths)
      -> array<System::String^>^ {
      auto result = gcnew array<System::String^>(
        static_cast<int>(paths.size()));
      for (std::size_t i = 0; i < paths.size(); ++i) {
        result[static_cast<int>(i)] = ToManagedString(paths[i].string());
      }
      return result;
    }
  } // namespace detail

  // PathFinderConfig -------------------------------------------------------
  public
  ref class PathFinderConfigManaged sealed {
  public:
    System::String^ WorkspaceRootPath;
    System::String^ ShaderLibraryPath;
    System::String^ CVarsArchivePath;
    array<System::String^>^ ScriptSourceRoots;
    System::String^ ScriptBytecodeCachePath;

    PathFinderConfigManaged()
      : WorkspaceRootPath(gcnew System::String("")),
      ShaderLibraryPath(gcnew System::String("")),
      CVarsArchivePath(gcnew System::String("")),
      ScriptSourceRoots(gcnew array<System::String^>(0)),
      ScriptBytecodeCachePath(gcnew System::String("")) {
    }

    static PathFinderConfigManaged^ FromNative(
      native::PathFinderConfig const& n) {
      auto m = gcnew PathFinderConfigManaged();
      m->WorkspaceRootPath = detail::ToManagedString(n.WorkspaceRootPath().string());
      m->ShaderLibraryPath = detail::ToManagedString(n.ShaderLibraryPath().string());
      m->CVarsArchivePath = detail::ToManagedString(n.CVarsArchivePath().string());
      m->ScriptSourceRoots = detail::ToManagedStringArray(n.ScriptSourceRoots());
      m->ScriptBytecodeCachePath =
        detail::ToManagedString(n.ScriptBytecodeCachePath().string());
      return m;
    }

    native::PathFinderConfig ToNative() {
      auto builder = native::PathFinderConfig::Create();
      if (!System::String::IsNullOrWhiteSpace(WorkspaceRootPath)) {
        builder = std::move(builder).WithWorkspaceRoot(
          detail::ToStdString(WorkspaceRootPath));
      }
      if (!System::String::IsNullOrWhiteSpace(ShaderLibraryPath)) {
        builder = std::move(builder).WithShaderLibraryPath(
          detail::ToStdString(ShaderLibraryPath));
      }
      if (!System::String::IsNullOrWhiteSpace(CVarsArchivePath)) {
        builder = std::move(builder).WithCVarsArchivePath(
          detail::ToStdString(CVarsArchivePath));
      }
      if (ScriptSourceRoots != nullptr && ScriptSourceRoots->Length > 0) {
        builder = std::move(builder).WithScriptSourceRoots(
          detail::ToPathVector(ScriptSourceRoots));
      }
      if (!System::String::IsNullOrWhiteSpace(ScriptBytecodeCachePath)) {
        builder = std::move(builder).WithScriptBytecodeCachePath(
          detail::ToStdString(ScriptBytecodeCachePath));
      }
      return std::move(builder).Build();
    }
  };

  // RendererConfig ---------------------------------------------------------
  public enum class ShadowQualityTierManaged : System::Byte {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3,
  };

  public enum class DirectionalShadowImplementationPolicyManaged : System::Byte {
    ConventionalOnly = 0,
    VirtualShadowMap = 1,
  };

  public
  ref class RendererConfigManaged sealed {
  public:
    PathFinderConfigManaged^ PathFinder;
    System::String^ UploadQueueKey; // required
    System::UInt64 MaxActiveViews;
    ShadowQualityTierManaged ShadowQualityTier;
    DirectionalShadowImplementationPolicyManaged DirectionalShadowPolicy;
    bool EnableImGui;

    RendererConfigManaged()
      : PathFinder(gcnew PathFinderConfigManaged()),
      UploadQueueKey(gcnew System::String("")), MaxActiveViews(8),
      ShadowQualityTier(ShadowQualityTierManaged::High),
      DirectionalShadowPolicy(DirectionalShadowImplementationPolicyManaged::ConventionalOnly),
      EnableImGui(false) {
    }

    static RendererConfigManaged^ FromNative(native::RendererConfig const& n) {
      auto m = gcnew RendererConfigManaged();
      m->PathFinder = PathFinderConfigManaged::FromNative(n.path_finder_config);
      m->UploadQueueKey = detail::ToManagedString(n.upload_queue_key);
      m->MaxActiveViews = n.max_active_views;
      m->ShadowQualityTier =
        static_cast<ShadowQualityTierManaged>(n.shadow_quality_tier);
      m->DirectionalShadowPolicy =
        static_cast<DirectionalShadowImplementationPolicyManaged>(
          n.directional_shadow_policy);
      m->EnableImGui = n.enable_imgui;
      return m;
    }

    native::RendererConfig ToNative() {
      native::RendererConfig n;
      n.path_finder_config = PathFinder != nullptr
        ? PathFinder->ToNative()
        : native::PathFinderConfig {};
      n.upload_queue_key = detail::ToStdString(UploadQueueKey);
      n.max_active_views = static_cast<std::size_t>(MaxActiveViews);
      n.shadow_quality_tier =
        static_cast<native::ShadowQualityTier>(ShadowQualityTier);
      n.directional_shadow_policy =
        static_cast<native::DirectionalShadowImplementationPolicy>(
          DirectionalShadowPolicy);
      n.enable_imgui = EnableImGui;
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

  // FrameCaptureConfig -----------------------------------------------------
  public enum class FrameCaptureProviderManaged : System::Byte {
    None = 0,
    RenderDoc = 1,
    Pix = 2,
  };

  public enum class FrameCaptureInitModeManaged : System::Byte {
    Disabled = 0,
    AttachedOnly = 1,
    Search = 2,
    ExplicitPath = 3,
  };

  public
  ref class FrameCaptureConfigManaged sealed {
  public:
    FrameCaptureProviderManaged Provider;
    FrameCaptureInitModeManaged InitMode;
    System::UInt64 FromFrame;
    System::UInt32 FrameCount;
    System::String^ ModulePath;
    System::String^ CaptureFileTemplate;

    FrameCaptureConfigManaged()
      : Provider(FrameCaptureProviderManaged::None),
      InitMode(FrameCaptureInitModeManaged::Disabled), FromFrame(0),
      FrameCount(0), ModulePath(gcnew System::String("")),
      CaptureFileTemplate(gcnew System::String("")) {
    }

    static FrameCaptureConfigManaged^ FromNative(
      native::FrameCaptureConfig const& n) {
      auto m = gcnew FrameCaptureConfigManaged();
      m->Provider = static_cast<FrameCaptureProviderManaged>(n.provider);
      m->InitMode = static_cast<FrameCaptureInitModeManaged>(n.init_mode);
      m->FromFrame = n.from_frame;
      m->FrameCount = n.frame_count;
      m->ModulePath = detail::ToManagedString(n.module_path);
      m->CaptureFileTemplate = detail::ToManagedString(n.capture_file_template);
      return m;
    }

    native::FrameCaptureConfig ToNative() {
      native::FrameCaptureConfig n;
      n.provider = static_cast<native::FrameCaptureProvider>(Provider);
      n.init_mode = static_cast<native::FrameCaptureInitMode>(InitMode);
      n.from_frame = FromFrame;
      n.frame_count = FrameCount;
      n.module_path = ModulePath != nullptr
        ? detail::ToStdString(ModulePath)
        : std::string {};
      n.capture_file_template = CaptureFileTemplate != nullptr
        ? detail::ToStdString(CaptureFileTemplate)
        : std::string {};
      return n;
    }
  };

  // GraphicsConfig ---------------------------------------------------------
  public
  ref class GraphicsConfigManaged sealed {
  public:
    bool EnableDebug;
    bool EnableValidation;
    bool EnableAftermath;
    System::String^ PreferredCardName;                // null => not specified
    System::Nullable<long long> PreferredCardDeviceId; // null => not specified
    bool Headless;
    bool EnableImGui;
    bool EnableVSync;
    FrameCaptureConfigManaged^ FrameCapture;
    System::String^ Extra; // JSON string

    GraphicsConfigManaged()
      : EnableDebug(native::DefaultGraphicsDebugLayerEnabled()),
      EnableValidation(false), EnableAftermath(native::DefaultGraphicsAftermathEnabled()),
      PreferredCardName(nullptr), PreferredCardDeviceId(), Headless(false), EnableImGui(false),
      EnableVSync(true), FrameCapture(gcnew FrameCaptureConfigManaged()),
      Extra(gcnew System::String("{}")) {
    }

    static GraphicsConfigManaged^ FromNative(native::GraphicsConfig const& n) {
      auto m = gcnew GraphicsConfigManaged();
      m->EnableDebug = n.enable_debug_layer;
      m->EnableValidation = n.enable_validation;
      m->EnableAftermath = n.enable_aftermath;
      if (n.preferred_card_name.has_value()) {
        m->PreferredCardName = detail::ToManagedString(*n.preferred_card_name);
      }
      if (n.preferred_card_device_id.has_value()) {
        m->PreferredCardDeviceId =
          System::Nullable<long long>(*n.preferred_card_device_id);
      }
      m->Headless = n.headless;
      m->EnableImGui = n.enable_imgui;
      m->EnableVSync = n.enable_vsync;
      m->FrameCapture = FrameCaptureConfigManaged::FromNative(n.frame_capture);
      m->Extra = detail::ToManagedString(n.extra);
      return m;
    }

    native::GraphicsConfig ToNative() {
      native::GraphicsConfig n;
      n.enable_debug_layer = EnableDebug;
      n.enable_validation = EnableValidation;
      n.enable_aftermath = EnableAftermath;
      if (!System::String::IsNullOrEmpty(PreferredCardName)) {
        n.preferred_card_name = detail::ToStdString(PreferredCardName);
      }
      if (PreferredCardDeviceId.HasValue) {
        n.preferred_card_device_id =
          static_cast<native::DeviceId>(PreferredCardDeviceId.Value);
      }
      n.headless = Headless;
      n.enable_imgui = EnableImGui;
      n.enable_vsync = EnableVSync;
      n.frame_capture = FrameCapture != nullptr
        ? FrameCapture->ToNative()
        : native::FrameCaptureConfig {};
      n.extra = Extra != nullptr ? detail::ToStdString(Extra) : std::string("{}");
      return n;
    }
  };

  // PlatformConfig ---------------------------------------------------------
  public
  ref class PlatformConfigManaged sealed {
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
  public
  ref class TimingConfigManaged sealed {
  public:
    System::TimeSpan FixedDelta;         // canonical (nanoseconds)
    System::TimeSpan MaxAccumulator;     // canonical (nanoseconds)
    System::UInt32 MaxSubsteps;          // max iterations / frame
    System::TimeSpan PacingSafetyMargin; // microseconds
    System::TimeSpan UncappedCooperativeSleep; // milliseconds

    TimingConfigManaged()
      : FixedDelta(System::TimeSpan::FromTicks(0)),
      MaxAccumulator(System::TimeSpan::FromTicks(0)), MaxSubsteps(0),
      PacingSafetyMargin(System::TimeSpan::FromTicks(0)),
      UncappedCooperativeSleep(System::TimeSpan::FromTicks(0)) {
    }

    static TimingConfigManaged^ FromNative(native::TimingConfig const& n) {
      auto m = gcnew TimingConfigManaged();
      // NamedType wraps chrono::nanoseconds
      m->FixedDelta = detail::NsToTimeSpan(n.fixed_delta.get());
      m->MaxAccumulator = detail::NsToTimeSpan(n.max_accumulator.get());
      m->MaxSubsteps = n.max_substeps;
      m->PacingSafetyMargin =
        detail::MicrosecondsToTimeSpan(n.pacing_safety_margin);
      m->UncappedCooperativeSleep =
        detail::MillisecondsToTimeSpan(n.uncapped_cooperative_sleep);
      return m;
    }

    native::TimingConfig ToNative() {
      native::TimingConfig n;
      n.fixed_delta =
        native::time::CanonicalDuration{ detail::TimeSpanToNs(FixedDelta) };
      n.max_accumulator =
        native::time::CanonicalDuration{ detail::TimeSpanToNs(MaxAccumulator) };
      n.max_substeps = MaxSubsteps;
      n.pacing_safety_margin = detail::TimeSpanToMicroseconds(PacingSafetyMargin);
      n.uncapped_cooperative_sleep =
        detail::TimeSpanToMilliseconds(UncappedCooperativeSleep);
      return n;
    }
  };

  // EngineConfig -----------------------------------------------------------
  public enum class RendererImplementationManaged : System::Byte {
    Legacy = 0,
    Vortex = 1,
  };

  public enum class PhysicsBackendManaged : System::Byte {
    None = 0,
    Jolt = 1,
    PhysX = 2,
  };

  public
  ref class EngineConfig sealed {
  public:
    RendererImplementationManaged RendererImplementation;

    // Application sub-structure
    System::String^ ApplicationName;
    System::UInt32 ApplicationVersion;

    System::UInt32 TargetFps;  // 0 = uncapped
    System::UInt32 FrameCount; // 0 = unlimited / run until exit
    System::Boolean EnableAssetLoader;
    System::Boolean VerifyAssetContentHashes;
    PhysicsBackendManaged PhysicsBackend;
    System::Boolean EnableScriptHotReload;
    System::TimeSpan ScriptHotReloadPollInterval;
    PathFinderConfigManaged^ PathFinder;

    GraphicsConfigManaged^ Graphics; // required logically; allow null -> default
    TimingConfigManaged^ Timing;     // required logically; allow null -> default

    EngineConfig()
      : RendererImplementation(RendererImplementationManaged::Legacy),
      ApplicationName(gcnew System::String("")), ApplicationVersion(0),
      TargetFps(0), FrameCount(0), EnableAssetLoader(false),
      VerifyAssetContentHashes(false), PhysicsBackend(PhysicsBackendManaged::Jolt),
      EnableScriptHotReload(true),
      ScriptHotReloadPollInterval(System::TimeSpan::FromMilliseconds(100.0)),
      PathFinder(gcnew PathFinderConfigManaged()), Graphics(nullptr),
      Timing(nullptr) {
    }

    static EngineConfig^ FromNative(native::EngineConfig const& n) {
      auto m = gcnew EngineConfig();
      m->RendererImplementation =
        static_cast<RendererImplementationManaged>(n.renderer.implementation);
      m->ApplicationName = detail::ToManagedString(n.application.name);
      m->ApplicationVersion = n.application.version;
      m->TargetFps = n.target_fps;
      m->FrameCount = n.frame_count;
      m->EnableAssetLoader = n.enable_asset_loader;
      m->VerifyAssetContentHashes = n.asset_loader.verify_content_hashes;
      m->PhysicsBackend = static_cast<PhysicsBackendManaged>(n.physics.backend);
      m->EnableScriptHotReload = n.scripting.enable_hot_reload;
      m->ScriptHotReloadPollInterval =
        detail::MillisecondsToTimeSpan(n.scripting.hot_reload_poll_interval);
      m->PathFinder = PathFinderConfigManaged::FromNative(n.path_finder_config);
      m->Graphics = GraphicsConfigManaged::FromNative(n.graphics);
      m->Timing = TimingConfigManaged::FromNative(n.timing);
      return m;
    }

    native::EngineConfig ToNative() {
      native::EngineConfig n; // value-init uses defaults from structs
      n.renderer.implementation =
        static_cast<native::RendererImplementation>(RendererImplementation);
      n.application.name = detail::ToStdString(ApplicationName);
      n.application.version = ApplicationVersion;
      n.target_fps = TargetFps;
      n.frame_count = FrameCount;
      n.enable_asset_loader = EnableAssetLoader;
      n.asset_loader.verify_content_hashes = VerifyAssetContentHashes;
      n.physics.backend =
        static_cast<native::EnginePhysicsBackend>(PhysicsBackend);
      n.scripting.enable_hot_reload = EnableScriptHotReload;
      n.scripting.hot_reload_poll_interval =
        detail::TimeSpanToMilliseconds(ScriptHotReloadPollInterval);
      n.path_finder_config = PathFinder != nullptr
        ? PathFinder->ToNative()
        : native::PathFinderConfig {};
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
      System::UInt32 get() { return native::EngineConfig::kMaxTargetFps; }
    }
  };

  // EditorEngineConfig -----------------------------------------------------
  public
  ref class EditorEngineConfigManaged sealed {
  public:
    PlatformConfigManaged^ Platform;
    EngineConfig^ Engine;
    RendererConfigManaged^ Renderer;

    EditorEngineConfigManaged()
      : Platform(gcnew PlatformConfigManaged()), Engine(gcnew EngineConfig()),
      Renderer(gcnew RendererConfigManaged()) {
    }

    static EditorEngineConfigManaged^ FromNative(
      native::engine::interop::EditorEngineConfig const& n) {
      auto m = gcnew EditorEngineConfigManaged();
      m->Platform = PlatformConfigManaged::FromNative(n.platform);
      m->Engine = EngineConfig::FromNative(n.engine);
      m->Renderer = RendererConfigManaged::FromNative(n.renderer);
      return m;
    }

    native::engine::interop::EditorEngineConfig ToNative() {
      native::engine::interop::EditorEngineConfig n;
      n.platform = Platform != nullptr
        ? Platform->ToNative()
        : native::PlatformConfig {};
      n.engine = Engine != nullptr ? Engine->ToNative() : native::EngineConfig {};
      n.renderer = Renderer != nullptr
        ? Renderer->ToNative()
        : native::RendererConfig {};
      return n;
    }
  };

  // Convenience aggregating helper (optional future extension point)
  public
  ref class ConfigFactory abstract
    sealed {
  public:
    static EngineConfig^ CreateDefaultEngineConfig() {
      native::EngineConfig native_default; // default constructed
      return EngineConfig::FromNative(native_default);
    }

    static EditorEngineConfigManaged^ CreateDefaultEditorEngineConfig() {
      native::engine::interop::EditorEngineConfig native_default;
      native_default.platform.headless = native_default.engine.graphics.headless;
      native_default.renderer.path_finder_config =
        native_default.engine.path_finder_config;
      native_default.renderer.enable_imgui =
        native_default.engine.graphics.enable_imgui;
      return EditorEngineConfigManaged::FromNative(native_default);
    }
  };

} // namespace Oxygen::Interop

#pragma managed(pop)
