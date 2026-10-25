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

    /// <summary>Gets the scripts and assets the slots use, which the scene depends on.</summary>
    public IReadOnlyList<AssetPath> ScriptReferences { get; init; } = [];
}

/// <summary>Where a script sidecar binding comes from, so cook diagnostics can point at the slot.</summary>
/// <param name="NodeId">The node; for an instance binding, the instance root.</param>
/// <param name="NodeName">The node's name.</param>
/// <param name="SlotId">The slot.</param>
/// <param name="SlotNumber">The slot's 1-based position on the node; 0 for a slot of an instanced node, which its source
/// numbers.</param>
/// <param name="ParameterNames">The names of the binding's parameter values, in binding order.</param>
public sealed record ScriptBindingSite(NodeId NodeId, string NodeName, Guid SlotId, int SlotNumber, IReadOnlyList<string> ParameterNames)
{
    /// <summary>Gets the instanced node below <see cref="NodeId"/> whose slot an instance binding changes; empty for a slot
    /// of the node itself.</summary>
    public NodePath Path { get; init; } = NodePath.Empty;

    /// <summary>Gets the instanced node's last known names, or <see langword="null"/> when they are not known.</summary>
    public string? InstancedNames { get; init; }
}
