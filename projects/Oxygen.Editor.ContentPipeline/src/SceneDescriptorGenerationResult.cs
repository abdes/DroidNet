// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result of native scene descriptor generation.
/// </summary>
public sealed record SceneDescriptorGenerationResult(
    Uri SceneAssetUri,
    string DescriptorPath,
    string DescriptorVirtualPath,
    IReadOnlyList<ContentCookInput> Dependencies,
    IReadOnlyList<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets the scene's identity, for diagnostics that point into it.</summary>
    public Guid SceneId { get; init; }

    /// <summary>Gets the scene's name.</summary>
    public string SceneName { get; init; } = string.Empty;

    /// <summary>Gets the scene's script slots, for its script sidecar job; empty when no node has scripts.</summary>
    public IReadOnlyList<ScriptSidecarBinding> ScriptBindings { get; init; } = [];

    /// <summary>Gets the node and slot each binding comes from, in binding order.</summary>
    public IReadOnlyList<ScriptBindingSite> ScriptBindingSites { get; init; } = [];

    /// <summary>Gets the scripts and assets the slots use, which the scene depends on.</summary>
    public IReadOnlyList<Uri> ScriptReferences { get; init; } = [];
}

/// <summary>Where a script sidecar binding comes from, so cook diagnostics can point at the slot.</summary>
/// <param name="NodeId">The node.</param>
/// <param name="NodeName">The node's name.</param>
/// <param name="SlotId">The slot.</param>
/// <param name="SlotNumber">The slot's 1-based position on the node.</param>
/// <param name="ParameterNames">The names of the binding's parameter values, in binding order.</param>
public sealed record ScriptBindingSite(Guid NodeId, string NodeName, Guid SlotId, int SlotNumber, IReadOnlyList<string> ParameterNames);
