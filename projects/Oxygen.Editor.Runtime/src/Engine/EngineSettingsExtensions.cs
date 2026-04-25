// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Helpers for translating editor settings to native interop config wrappers.
/// </summary>
internal static class EngineSettingsExtensions
{
    /// <summary>
    ///     Applies editor settings to the managed native startup configuration wrapper.
    /// </summary>
    /// <param name="settings">The editor engine settings.</param>
    /// <param name="config">The managed native startup configuration wrapper.</param>
    public static void ApplyTo(this IEngineSettings settings, EditorEngineConfigManaged config)
    {
        ArgumentNullException.ThrowIfNull(settings);
        ArgumentNullException.ThrowIfNull(config);

        config.Platform ??= new PlatformConfigManaged();
        config.Engine ??= new EngineConfig();
        config.Renderer ??= new RendererConfigManaged();
        config.Engine.Graphics ??= new GraphicsConfigManaged();
        config.Engine.Timing ??= new TimingConfigManaged();

        settings.Platform.ApplyTo(config.Platform);
        settings.Engine.ApplyTo(config.Engine);
        settings.Timing.ApplyTo(config.Engine.Timing);
        settings.Graphics.ApplyTo(config.Engine.Graphics);
        settings.Renderer.ApplyTo(config.Engine, config.Renderer);
    }

    private static void ApplyTo(this PlatformSettings settings, PlatformConfigManaged config)
    {
        if (settings.Headless.HasValue)
        {
            config.Headless = settings.Headless.Value;
        }

        if (settings.ThreadPoolSize.HasValue)
        {
            config.ThreadPoolSize = settings.ThreadPoolSize.Value;
        }
    }

    private static void ApplyTo(this EngineCoreSettings settings, EngineConfig config)
    {
        if (!string.IsNullOrWhiteSpace(settings.ApplicationName))
        {
            config.ApplicationName = settings.ApplicationName;
        }

        if (settings.ApplicationVersion.HasValue)
        {
            config.ApplicationVersion = settings.ApplicationVersion.Value;
        }

        if (settings.TargetFps.HasValue)
        {
            config.TargetFps = settings.TargetFps.Value;
        }

        if (settings.FrameCount.HasValue)
        {
            config.FrameCount = settings.FrameCount.Value;
        }

        if (settings.EnableAssetLoader.HasValue)
        {
            config.EnableAssetLoader = settings.EnableAssetLoader.Value;
        }

        if (settings.VerifyAssetContentHashes.HasValue)
        {
            config.VerifyAssetContentHashes = settings.VerifyAssetContentHashes.Value;
        }

        if (settings.PhysicsBackend.HasValue)
        {
            config.PhysicsBackend = settings.PhysicsBackend.Value;
        }

        if (settings.EnableScriptHotReload.HasValue)
        {
            config.EnableScriptHotReload = settings.EnableScriptHotReload.Value;
        }

        if (settings.ScriptHotReloadPollInterval.HasValue)
        {
            config.ScriptHotReloadPollInterval = settings.ScriptHotReloadPollInterval.Value;
        }

        if (settings.PathFinder.HasAnySetting())
        {
            config.PathFinder ??= new PathFinderConfigManaged();
            settings.PathFinder.ApplyTo(config.PathFinder);
        }
    }

    private static void ApplyTo(this TimingSettings settings, TimingConfigManaged config)
    {
        if (settings.FixedDelta.HasValue)
        {
            config.FixedDelta = settings.FixedDelta.Value;
        }

        if (settings.MaxAccumulator.HasValue)
        {
            config.MaxAccumulator = settings.MaxAccumulator.Value;
        }

        if (settings.MaxSubsteps.HasValue)
        {
            config.MaxSubsteps = settings.MaxSubsteps.Value;
        }

        if (settings.PacingSafetyMargin.HasValue)
        {
            config.PacingSafetyMargin = settings.PacingSafetyMargin.Value;
        }

        if (settings.UncappedCooperativeSleep.HasValue)
        {
            config.UncappedCooperativeSleep = settings.UncappedCooperativeSleep.Value;
        }
    }

    private static void ApplyTo(this RendererSettings settings, EngineConfig engine, RendererConfigManaged config)
    {
        if (settings.Implementation.HasValue)
        {
            engine.RendererImplementation = settings.Implementation.Value;
        }

        if (!string.IsNullOrWhiteSpace(settings.UploadQueueKey))
        {
            config.UploadQueueKey = settings.UploadQueueKey;
        }

        if (settings.MaxActiveViews.HasValue)
        {
            config.MaxActiveViews = settings.MaxActiveViews.Value;
        }

        if (settings.ShadowQualityTier.HasValue)
        {
            config.ShadowQualityTier = settings.ShadowQualityTier.Value;
        }

        if (settings.DirectionalShadowPolicy.HasValue)
        {
            config.DirectionalShadowPolicy = settings.DirectionalShadowPolicy.Value;
        }

        if (settings.EnableImGui.HasValue)
        {
            config.EnableImGui = settings.EnableImGui.Value;
        }

        if (settings.PathFinder.HasAnySetting())
        {
            config.PathFinder ??= new PathFinderConfigManaged();
            settings.PathFinder.ApplyTo(config.PathFinder);
        }
        else
        {
            config.PathFinder = engine.PathFinder;
        }
    }

    private static void ApplyTo(this GraphicsSettings settings, GraphicsConfigManaged config)
    {
        if (settings.EnableDebugLayer.HasValue)
        {
            config.EnableDebug = settings.EnableDebugLayer.Value;
        }

        if (settings.EnableValidation.HasValue)
        {
            config.EnableValidation = settings.EnableValidation.Value;
        }

        if (settings.EnableAftermath.HasValue)
        {
            config.EnableAftermath = settings.EnableAftermath.Value;
            if (settings.EnableAftermath.Value && !settings.EnableDebugLayer.HasValue)
            {
                config.EnableDebug = false;
            }
        }

        if (config.EnableDebug && config.EnableAftermath)
        {
            throw new InvalidOperationException("Graphics debug layer and Nsight Aftermath are mutually exclusive. Disable one before starting the engine.");
        }

        if (!string.IsNullOrWhiteSpace(settings.PreferredCardName))
        {
            config.PreferredCardName = settings.PreferredCardName;
        }

        if (settings.PreferredCardDeviceId.HasValue)
        {
            config.PreferredCardDeviceId = settings.PreferredCardDeviceId.Value;
        }

        if (settings.Headless.HasValue)
        {
            config.Headless = settings.Headless.Value;
        }

        if (settings.EnableImGui.HasValue)
        {
            config.EnableImGui = settings.EnableImGui.Value;
        }

        if (settings.EnableVSync.HasValue)
        {
            config.EnableVSync = settings.EnableVSync.Value;
        }

        config.FrameCapture ??= new FrameCaptureConfigManaged();
        settings.FrameCapture.ApplyTo(config.FrameCapture);

        if (!string.IsNullOrWhiteSpace(settings.Extra))
        {
            config.Extra = settings.Extra;
        }
    }

    private static void ApplyTo(this FrameCaptureSettings settings, FrameCaptureConfigManaged config)
    {
        if (settings.Provider.HasValue)
        {
            config.Provider = settings.Provider.Value;
        }

        if (settings.InitMode.HasValue)
        {
            config.InitMode = settings.InitMode.Value;
        }

        if (settings.FromFrame.HasValue)
        {
            config.FromFrame = settings.FromFrame.Value;
        }

        if (settings.FrameCount.HasValue)
        {
            config.FrameCount = settings.FrameCount.Value;
        }

        if (!string.IsNullOrWhiteSpace(settings.ModulePath))
        {
            config.ModulePath = settings.ModulePath;
        }

        if (!string.IsNullOrWhiteSpace(settings.CaptureFileTemplate))
        {
            config.CaptureFileTemplate = settings.CaptureFileTemplate;
        }
    }

    private static void ApplyTo(this PathFinderSettings settings, PathFinderConfigManaged config)
    {
        if (!string.IsNullOrWhiteSpace(settings.WorkspaceRootPath))
        {
            config.WorkspaceRootPath = settings.WorkspaceRootPath;
        }

        if (!string.IsNullOrWhiteSpace(settings.ShaderLibraryPath))
        {
            config.ShaderLibraryPath = settings.ShaderLibraryPath;
        }

        if (!string.IsNullOrWhiteSpace(settings.CVarsArchivePath))
        {
            config.CVarsArchivePath = settings.CVarsArchivePath;
        }

        if (settings.ScriptSourceRoots.Count > 0)
        {
            config.ScriptSourceRoots = settings.ScriptSourceRoots.ToArray();
        }

        if (!string.IsNullOrWhiteSpace(settings.ScriptBytecodeCachePath))
        {
            config.ScriptBytecodeCachePath = settings.ScriptBytecodeCachePath;
        }
    }

    private static bool HasAnySetting(this PathFinderSettings settings)
        => !string.IsNullOrWhiteSpace(settings.WorkspaceRootPath)
            || !string.IsNullOrWhiteSpace(settings.ShaderLibraryPath)
            || !string.IsNullOrWhiteSpace(settings.CVarsArchivePath)
            || settings.ScriptSourceRoots.Count > 0
            || !string.IsNullOrWhiteSpace(settings.ScriptBytecodeCachePath);
}
