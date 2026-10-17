// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Renames a node.</summary>
/// <param name="Node">The node.</param>
/// <param name="Name">The new name.</param>
public sealed record RenameNode(NodeId Node, string Name) : ISceneEdit
{
    /// <inheritdoc/>
    public string Label => $"Rename to '{this.Name}'";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Node];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return string.IsNullOrWhiteSpace(this.Name)
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NameEmpty, "Scene node names cannot be empty.", this.Node))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var node = scope.FindNode(this.Node)!;
        var name = this.Name.Trim();
        var before = node.Name;
        if (string.Equals(before, name, StringComparison.Ordinal))
        {
            return new SceneEditApplication.Applied(this, []);
        }

        node.Name = name;
        return new SceneEditApplication.Applied(this with { Name = before }, [new SceneChange.NodeRenamed(this.Node)]);
    }
}
