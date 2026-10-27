// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

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
