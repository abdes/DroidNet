// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of a node's authored rendering flags, which belong to the node itself.</summary>
internal sealed class NodeRenderingRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneSchema.Node.Kind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.NodeRendering.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => node;
}
