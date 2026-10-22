// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The required component that names its node, the engine's <c>ObjectMetadata</c>. Its <see cref="GameComponent.Name"/>
/// is the node's name: nonempty after trimming, and never an identity, since names may repeat.
/// </summary>
public sealed class MetadataComponent : GameComponent
{
    static MetadataComponent()
    {
        Register<MetadataData>(d =>
        {
            var metadata = new MetadataComponent { Name = d.Name };
            metadata.Hydrate(d);
            return metadata;
        });
    }

    /// <summary>
    /// Gets a value indicating whether the component can be removed; a required component cannot.
    /// </summary>
    public override bool IsLocked => true;

    /// <inheritdoc/>
    public override ComponentData Dehydrate() => new MetadataData { Id = this.Id, Name = this.Name };

    /// <inheritdoc/>
    /// <exception cref="ArgumentException">The name is empty or only white space.</exception>
    protected override void ValidateName(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("A node name must not be empty or only white space.", nameof(value));
        }
    }
}
