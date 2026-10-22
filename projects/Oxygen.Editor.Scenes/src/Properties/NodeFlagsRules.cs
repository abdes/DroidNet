// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of a node's authored flags, held by its node-flags component.</summary>
internal sealed class NodeFlagsRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneSchema.Node.Kind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.NodeFlags.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => node?.Flags;
}
