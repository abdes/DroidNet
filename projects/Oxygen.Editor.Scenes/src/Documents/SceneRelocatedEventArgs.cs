// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Documents;

/// <summary>Names what the engine must show again after a scene followed a relocation.</summary>
/// <param name="geometryNodes">The nodes whose geometry or materials moved.</param>
/// <param name="environment">Whether the scene's environment references moved.</param>
public sealed class SceneRelocatedEventArgs(IReadOnlyList<NodeId> geometryNodes, bool environment) : EventArgs
{
    /// <summary>Gets the nodes whose geometry or materials moved.</summary>
    public IReadOnlyList<NodeId> GeometryNodes { get; } = geometryNodes;

    /// <summary>Gets a value indicating whether the scene's environment references moved.</summary>
    public bool Environment { get; } = environment;
}
