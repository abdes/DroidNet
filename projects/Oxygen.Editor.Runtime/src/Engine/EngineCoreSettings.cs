// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Core engine settings for native engine startup.
/// </summary>
public sealed class EngineCoreSettings
{
    /// <summary>
    ///     Gets or sets the application name reported to the engine.
    /// </summary>
    public string? ApplicationName { get; set; }

    /// <summary>
    ///     Gets or sets the application version reported to the engine.
    /// </summary>
    public uint? ApplicationVersion { get; set; }

    /// <summary>
    ///     Gets or sets the target frame rate. Zero means uncapped.
    /// </summary>
    public uint? TargetFps { get; set; }

    /// <summary>
    ///     Gets or sets the fixed frame count. Zero means unlimited.
    /// </summary>
    public uint? FrameCount { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the engine asset loader is enabled.
    /// </summary>
    public bool? EnableAssetLoader { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether mounted asset content hashes are verified.
    /// </summary>
    public bool? VerifyAssetContentHashes { get; set; }

    /// <summary>
    ///     Gets or sets the requested physics backend.
    /// </summary>
    public PhysicsBackendManaged? PhysicsBackend { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether script hot reload is enabled.
    /// </summary>
    public bool? EnableScriptHotReload { get; set; }

    /// <summary>
    ///     Gets or sets the script hot reload polling interval.
    /// </summary>
    public TimeSpan? ScriptHotReloadPollInterval { get; set; }

    /// <summary>
    ///     Gets or sets path resolution settings shared by engine systems.
    /// </summary>
    public PathFinderSettings PathFinder { get; set; } = new();
}
