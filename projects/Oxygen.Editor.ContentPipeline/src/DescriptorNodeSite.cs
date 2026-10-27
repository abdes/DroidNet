// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>A node of a scene descriptor, which the cooker's diagnostics name by its index.</summary>
/// <param name="NodeId">The node's identity in the authored scene.</param>
/// <param name="NodeName">The node's name.</param>
public sealed record DescriptorNodeSite(NodeId NodeId, string NodeName)
{
    /// <summary>Gets the changes an instance root writes, in descriptor order; empty for any other node.</summary>
    public IReadOnlyList<DescriptorInstanceChange> InstanceChanges { get; init; } = [];
}
