// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Creates native settings only after entering the native session.</summary>
internal sealed partial class NativeEngineSession
{
    /// <inheritdoc />
    public override int LoggingVerbosity
    {
        get => this.Runner.GetLoggingConfig(this.context).Verbosity;
        set
        {
            var config = this.Runner.GetLoggingConfig(this.context);
            config.Verbosity = value;
            if (!this.Runner.ConfigureLogging(config))
            {
                throw new InvalidOperationException("Failed to configure native engine logging verbosity.");
            }
        }
    }

    /// <inheritdoc />
    public override uint MaxTargetFps => EngineConfig.MaxTargetFps;

    /// <inheritdoc />
    public override uint TargetFps
    {
        get => this.Runner.GetEngineConfig(this.context).TargetFps;
        set => this.Runner.SetTargetFps(this.context, value);
    }

    /// <inheritdoc />
    public override void SetVSyncEnabled(bool enabled) => this.Runner.SetVSyncEnabled(this.context, enabled);

    /// <inheritdoc />
    public override void SetAlwaysRenderPanes(bool alwaysRender)
    {
        if (!this.Runner.TrySetAlwaysRenderPanes(this.context, alwaysRender))
        {
            throw new InvalidOperationException("The native editor module is unavailable.");
        }
    }

    /// <summary>Builds editor startup configuration using the discovered SDK's data locations.</summary>
    /// <param name="settings">Authored engine settings, including explicit path overrides.</param>
    /// <param name="editorCVarsArchivePath">The editor's optional console archive.</param>
    /// <param name="runtimeLibrary">The runtime library selected and retained by the compatibility lease.</param>
    /// <returns>The native startup configuration.</returns>
    internal static EditorEngineConfigManaged CreateConfig(IEngineSettings settings, string? editorCVarsArchivePath, string runtimeLibrary)
    {
        var sdkRoot = Path.GetDirectoryName(Path.GetDirectoryName(runtimeLibrary))
            ?? throw new ArgumentException("The runtime library must be inside the SDK bin directory.", nameof(runtimeLibrary));
        var installation = new EditorNativeInstallation(AppContext.BaseDirectory, sdkRoot, EditorNativeCompatibilityService.CurrentConfiguration);
        var config = ConfigFactory.CreateDefaultEditorEngineConfig();
        config.Engine.TargetFps = EngineConstants.DefaultTargetFps;
        settings.ApplyTo(config);
        config.Engine.PathFinder ??= new PathFinderConfigManaged();
        if (string.IsNullOrWhiteSpace(settings.Engine.PathFinder.ShaderLibraryPath))
        {
            var shaders = installation.ShaderLibraryPath;
            if (!File.Exists(shaders))
            {
                throw new FileNotFoundException("The installed engine shader archive is missing.", shaders);
            }

            config.Engine.PathFinder.ShaderLibraryPath = shaders;
        }

        config.Renderer.PathFinder ??= new PathFinderConfigManaged();
        if (string.IsNullOrWhiteSpace(settings.Renderer.PathFinder.ShaderLibraryPath))
        {
            config.Renderer.PathFinder.ShaderLibraryPath = config.Engine.PathFinder.ShaderLibraryPath;
        }

        if (editorCVarsArchivePath is not null)
        {
            config.Engine.PathFinder ??= new PathFinderConfigManaged();
            if (ShouldUseEditorArchive(config.Engine.PathFinder.CVarsArchivePath))
            {
                config.Engine.PathFinder.CVarsArchivePath = editorCVarsArchivePath;
            }

            config.Renderer.PathFinder ??= config.Engine.PathFinder;
            if (ShouldUseEditorArchive(config.Renderer.PathFinder.CVarsArchivePath))
            {
                config.Renderer.PathFinder.CVarsArchivePath = editorCVarsArchivePath;
            }
        }

        config.Platform.Headless = true;
        config.Engine.Graphics.Headless = true;
        config.Engine.EnableAssetLoader = true;
        return config;
    }

    private static bool ShouldUseEditorArchive(string? path)
        => string.IsNullOrWhiteSpace(path) || string.Equals(path.Replace('\\', '/'), "bin/Oxygen/cvars.json", StringComparison.OrdinalIgnoreCase);
}
