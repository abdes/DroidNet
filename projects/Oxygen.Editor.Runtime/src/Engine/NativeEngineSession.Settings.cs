// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

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

    private static EditorEngineConfigManaged CreateConfig(IEngineSettings settings, string? editorCVarsArchivePath)
    {
        var config = ConfigFactory.CreateDefaultEditorEngineConfig();
        config.Engine.TargetFps = 1;
        settings.ApplyTo(config);
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
