// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Renderer module settings.
/// </summary>
public sealed class RendererSettings
{
    /// <summary>
    ///     Gets or sets the renderer implementation selected by the engine module config.
    /// </summary>
    public RendererImplementationManaged? Implementation { get; set; }

    /// <summary>
    ///     Gets or sets the upload queue key.
    /// </summary>
    public string? UploadQueueKey { get; set; }

    /// <summary>
    ///     Gets or sets the maximum number of active views retained by the renderer.
    /// </summary>
    public ulong? MaxActiveViews { get; set; }

    /// <summary>
    ///     Gets or sets the renderer shadow quality tier.
    /// </summary>
    public ShadowQualityTierManaged? ShadowQualityTier { get; set; }

    /// <summary>
    ///     Gets or sets the renderer directional shadow implementation policy.
    /// </summary>
    public DirectionalShadowImplementationPolicyManaged? DirectionalShadowPolicy { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether renderer-owned ImGui plumbing is enabled.
    /// </summary>
    public bool? EnableImGui { get; set; }

    /// <summary>
    ///     Gets or sets renderer path resolution settings.
    /// </summary>
    public PathFinderSettings PathFinder { get; set; } = new();
}
