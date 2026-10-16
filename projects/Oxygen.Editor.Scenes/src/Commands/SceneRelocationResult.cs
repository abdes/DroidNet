// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>What an open scene changed when it followed a relocation, and what its runtime view must show again.</summary>
/// <param name="Succeeded">Whether the scene followed the relocation.</param>
/// <param name="RuntimeNodes">The re-pointed nodes with geometry, which the runtime shows again once their assets are mounted.</param>
/// <param name="RuntimeEnvironment">Whether the environment was re-pointed, and the runtime updates it once its assets are mounted.</param>
public sealed record SceneRelocationResult(bool Succeeded, IReadOnlyList<NodeId> RuntimeNodes, bool RuntimeEnvironment)
{
    /// <summary>Gets the result of a scene that could not follow the relocation.</summary>
    public static SceneRelocationResult Failed { get; } = new(Succeeded: false, [], RuntimeEnvironment: false);
}
