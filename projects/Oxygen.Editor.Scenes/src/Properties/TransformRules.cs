// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of a node's transform.</summary>
internal sealed class TransformRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneSchema.Transform.Kind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.Transform.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => node?.Transform;

    /// <inheritdoc/>
    /// <remarks>
    /// Euler normalization can change another displayed axis while keeping one rotation, so an edit of one axis captures
    /// all three.
    /// </remarks>
    public IReadOnlySet<PropertyId> Coupled(IEnumerable<PropertyId> edited)
    {
        var ids = edited.ToHashSet();
        PropertyId[] rotation = [SceneSchema.Transform.RotationX.Id, SceneSchema.Transform.RotationY.Id, SceneSchema.Transform.RotationZ.Id];
        if (ids.Overlaps(rotation))
        {
            ids.UnionWith(rotation);
        }

        return ids;
    }
}
