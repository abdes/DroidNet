// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result of native scene descriptor generation.
/// </summary>
public sealed record SceneDescriptorGenerationResult(
    AssetPath SceneAssetUri,
    string DescriptorPath,
    string DescriptorVirtualPath,
    IReadOnlyList<ContentCookInput> Dependencies,
    IReadOnlyList<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets the scene's identity, for diagnostics that point into it.</summary>
    public SceneId SceneId { get; init; }

    /// <summary>Gets the scene's name.</summary>
    public string SceneName { get; init; } = string.Empty;

    /// <summary>Gets the scene's script slots, for its script sidecar job; empty when no node has scripts.</summary>
    public IReadOnlyList<ScriptSidecarBinding> ScriptBindings { get; init; } = [];

    /// <summary>Gets the script parameter changes of the scene's instances, for its script sidecar job.</summary>
    public IReadOnlyList<ScriptSidecarInstanceBinding> ScriptInstanceBindings { get; init; } = [];

    /// <summary>Gets a value indicating whether the scene needs a script sidecar job.</summary>
    public bool HasScriptSidecar => this.ScriptBindings.Count > 0 || this.ScriptInstanceBindings.Count > 0;

    /// <summary>Gets the node and slot each binding comes from, in binding order.</summary>
    public IReadOnlyList<ScriptBindingSite> ScriptBindingSites { get; init; } = [];

    /// <summary>Gets the instanced node and slot each instance binding comes from, in instance binding order.</summary>
    public IReadOnlyList<ScriptBindingSite> ScriptInstanceBindingSites { get; init; } = [];

    /// <summary>Gets each node of the descriptor, by its index there, for diagnostics that point at one.</summary>
    public IReadOnlyList<DescriptorNodeSite> Nodes { get; init; } = [];

    /// <summary>Gets the descriptor index of each renderable's node, by the renderable's index.</summary>
    public IReadOnlyList<int> RenderableNodes { get; init; } = [];

    /// <summary>Gets the scripts and assets the slots use, which the scene depends on.</summary>
    public IReadOnlyList<AssetPath> ScriptReferences { get; init; } = [];
}
