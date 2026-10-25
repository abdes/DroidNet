// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
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
        get;
        set => _ = this.SetProperty(ref field, value ?? throw new ArgumentNullException(nameof(value)));
    } = InstanceChanges.None;

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
            this.Changes = ReadChanges(instance.Changes);
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new SceneInstanceComponentData
        {
            Id = this.Id,
            Name = this.Name,
            Source = this.Source,
            Changes = this.Changes.IsEmpty ? null : [.. this.Changes.Ordered.Select(static entry => WriteChanges(entry.Key, entry.Value))],
        };

    private static InstanceChanges ReadChanges(IList<InstancedNodeChangesData>? data)
    {
        var changes = InstanceChanges.None;
        foreach (var node in data ?? [])
        {
            var path = node.Path.Count == 0
                ? throw new JsonException("An instance change names a node below the instance root: its path cannot be empty.")
                : NodePath.Of(node.Path);
            if (changes.Nodes.ContainsKey(path))
            {
                throw new JsonException($"The instance changes node {path} twice.");
            }

            var entry = InstancedNodeChanges.None.WithNames(node.Names).WithCarriedFrom(node.CarriedFrom);
            foreach (var (kind, overrides) in node.Components)
            {
                var componentChanges = SourceOverridesJson.ReadChanges(kind, overrides);
                entry = componentChanges.IsEmpty
                    ? throw new JsonException($"The changes of the {kind} component of node {path} hold no value.")
                    : entry.With(componentChanges);
            }

            changes = entry.IsEmpty
                ? throw new JsonException($"The changes of node {path} change no component.")
                : changes.With(path, entry);
        }

        return changes;
    }

    private static InstancedNodeChangesData WriteChanges(NodePath path, InstancedNodeChanges changes)
        => new()
        {
            Path = [.. path.Nodes],
            Names = changes.Names,
            CarriedFrom = changes.CarriedFrom,
            Components = changes.Components
                .OrderBy(static entry => entry.Key, StringComparer.Ordinal)
                .ToDictionary(static entry => entry.Key, static entry => SourceOverridesJson.Write(entry.Value)!, StringComparer.Ordinal),
        };
}
