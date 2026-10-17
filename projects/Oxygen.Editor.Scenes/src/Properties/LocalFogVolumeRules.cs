// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of a local fog volume: each field is validated by its descriptor alone.</summary>
internal sealed class LocalFogVolumeRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneProperties.LocalFogVolumeKind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.LocalFogVolume.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => node?.Components.OfType<LocalFogVolumeComponent>().FirstOrDefault();
}
