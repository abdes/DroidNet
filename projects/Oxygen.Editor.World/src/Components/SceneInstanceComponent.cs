// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Component that makes its node an instance root: the node brings in a source, an imported model or an authored scene,
/// whose root nodes become its children.
/// </summary>
/// <remarks>
/// The instanced nodes are not part of the scene model: they come from the source. The component holds every change the
/// scene makes of them, in <see cref="Changes"/>, by node path, and saves, loads, copies and undoes with the node like
/// any other component. A node has at most one.
/// </remarks>
public partial class SceneInstanceComponent : GameComponent
{
    private InstanceChanges changes = InstanceChanges.None;

    static SceneInstanceComponent()
    {
        // Register the factory that creates the component from its data when a scene is hydrated.
        Register<SceneInstanceComponentData>(d =>
        {
            var instance = new SceneInstanceComponent
            {
                Name = d.Name,
                Source = AssetPathData.Require(d.Source, nameof(d.Source)),
            };
            instance.Hydrate(d);
            return instance;
        });
    }

    /// <summary>
    /// Gets or sets the instanced source.
    /// </summary>
    /// <value>
    /// An absolute <c>asset:</c> URI: a model's source file, such as <c>asset:///Content/Vehicles/Car.glb</c>, or an
    /// authored scene, such as <c>asset:///Content/Prefabs/Watchtower.oscene</c>. Changing it keeps the changes; those of
    /// a node the new source does not have stay unresolved until they are removed.
    /// </value>
    /// <exception cref="ArgumentNullException">The value is <see langword="null"/>.</exception>
    public required AssetPath Source
    {
        get;
        set => _ = this.SetProperty(ref field, value ?? throw new ArgumentNullException(nameof(value)));
    }

    /// <summary>
    /// Gets or sets every change the scene makes below the instance root.
    /// </summary>
    /// <exception cref="ArgumentNullException">The value is <see langword="null"/>.</exception>
    public InstanceChanges Changes
    {
        get => this.changes;
        set => _ = this.SetProperty(ref this.changes, value ?? throw new ArgumentNullException(nameof(value)));
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not SceneInstanceComponentData instance)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.Source = AssetPathData.Require(instance.Source, nameof(instance.Source));
            this.Changes = InstanceChangesJson.Read(instance.Changes);
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new SceneInstanceComponentData
        {
            Id = this.Id,
            Name = this.Name,
            Source = this.Source,
            Changes = InstanceChangesJson.Write(this.Changes),
        };
}
