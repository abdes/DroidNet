// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
///     A scene node: an identity and a container of components, mirroring the engine's node.
/// </summary>
/// <remarks>
///     Every node holds exactly one each of the required <see cref="MetadataComponent"/> (its name),
///     <see cref="HierarchyComponent"/> (its parent and children), <see cref="TransformComponent"/> and
///     <see cref="NodeFlagsComponent"/>, created with the node and never removed; the other components are optional.
/// </remarks>
public sealed class SceneNode : ScopedObservableObject, IPersistent<SceneNodeData>
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNode" /> class with its required components.
    /// </summary>
    /// <param name="scene">The scene associated with the scene node.</param>
    /// <param name="name">The node's name, nonempty after trimming.</param>
    /// <exception cref="ArgumentException">The name is empty or only white space.</exception>
    public SceneNode(Scene scene, string name)
    {
        this.Scene = scene;
        this.Metadata = new MetadataComponent { Name = name, Node = this };
        this.Hierarchy = new HierarchyComponent { Name = HierarchyComponent.DefaultName, Node = this };
        this.Transform = new TransformComponent { Name = nameof(TransformComponent), Node = this };
        this.Flags = new NodeFlagsComponent { Name = NodeFlagsComponent.DefaultName, Node = this };
        this.Components = [this.Metadata, this.Hierarchy, this.Transform, this.Flags];
    }

    /// <summary>
    ///     Gets the identity of the node, authored in the scene file.
    /// </summary>
    public NodeId Id { get; init; } = NodeId.New();

    /// <summary>
    ///     Gets the scene associated with the scene node.
    /// </summary>
    [JsonIgnore]
    public Scene Scene { get; }

    /// <summary>
    ///     Gets the node's required metadata component, which holds its name.
    /// </summary>
    public MetadataComponent Metadata { get; }

    /// <summary>
    ///     Gets the node's required hierarchy component, which holds its parent and children.
    /// </summary>
    public HierarchyComponent Hierarchy { get; }

    /// <summary>
    ///     Gets the node's required transform component.
    /// </summary>
    public TransformComponent Transform { get; }

    /// <summary>
    ///     Gets the node's required node-flags component.
    /// </summary>
    public NodeFlagsComponent Flags { get; }

    /// <summary>
    ///     Gets the node's components, required ones first.
    /// </summary>
    public ObservableCollection<GameComponent> Components { get; }

    /// <summary>
    ///     Gets the node-scope override slots.
    /// </summary>
    public ObservableCollection<OverrideSlot> OverrideSlots { get; } = [];

    /// <summary>
    ///     Creates a node from its DTO, with its descendants.
    /// </summary>
    /// <param name="scene">The scene to associate with the new node.</param>
    /// <param name="data">The data transfer object to hydrate from.</param>
    /// <returns>A hydrated <see cref="SceneNode"/> instance.</returns>
    /// <exception cref="System.Text.Json.JsonException">A required component is missing or repeated.</exception>
    public static SceneNode CreateAndHydrate(Scene scene, SceneNodeData data)
    {
        ArgumentNullException.ThrowIfNull(data);
        var node = new SceneNode(scene, data.Required<MetadataData>().Name) { Id = data.Id };
        node.Hydrate(data);
        return node;
    }

    /// <summary>
    ///     Adds an optional component to this node and sets the component's <see cref="GameComponent.Node"/> property.
    /// </summary>
    /// <param name="component">The component to add.</param>
    /// <returns>True if the component was added; false if the node already holds it.</returns>
    /// <exception cref="InvalidOperationException">The component is a required one, which a node holds from creation.</exception>
    public bool AddComponent(GameComponent component)
    {
        ArgumentNullException.ThrowIfNull(component);
        if (this.Components.Contains(component))
        {
            return false;
        }

        if (IsRequired(component))
        {
            throw new InvalidOperationException($"A node holds its {component.GetType().Name} from creation; it cannot be added.");
        }

        this.Components.Add(component);
        component.Node = this;
        return true;
    }

    /// <summary>
    ///     Removes an optional component from this node and clears its owner.
    /// </summary>
    /// <param name="component">The component to remove.</param>
    /// <returns>True if the component was removed; otherwise false.</returns>
    /// <exception cref="InvalidOperationException">The component is one of the node's required components.</exception>
    public bool RemoveComponent(GameComponent component)
    {
        ArgumentNullException.ThrowIfNull(component);
        if (IsRequired(component) && this.Components.Contains(component))
        {
            throw new InvalidOperationException($"A node's {component.GetType().Name} cannot be removed.");
        }

        var removed = this.Components.Remove(component);
        if (removed)
        {
            component.Node = null;
        }

        return removed;
    }

    /// <summary>
    ///     Hydrates this scene node from the specified DTO: the required components in place, the optional components
    ///     recreated, and the children of the hierarchy created and appended.
    /// </summary>
    /// <param name="data">The node DTO to hydrate from.</param>
    /// <exception cref="System.Text.Json.JsonException">A required component is missing or repeated.</exception>
    public void Hydrate(SceneNodeData data)
    {
        ArgumentNullException.ThrowIfNull(data);
        var hierarchy = data.Required<HierarchyData>();
        using (this.SuppressNotifications())
        {
            this.Metadata.Hydrate(data.Required<MetadataData>());
            this.Hierarchy.Hydrate(hierarchy);
            this.Transform.Hydrate(data.Required<TransformData>());
            this.Flags.Hydrate(data.Required<NodeFlagsData>());

            this.Components.Clear();
            this.Components.Add(this.Metadata);
            this.Components.Add(this.Hierarchy);
            this.Components.Add(this.Transform);
            this.Components.Add(this.Flags);
            this.HydrateOptionalComponents(data.Components);

            this.OverrideSlots.Clear();
            foreach (var slotData in data.OverrideSlots)
            {
                this.OverrideSlots.Add(OverrideSlot.CreateAndHydrate(slotData));
            }

            foreach (var childData in hierarchy.Children ?? [])
            {
                this.Hierarchy.AddChild(CreateAndHydrate(this.Scene, childData));
            }
        }
    }

    /// <summary>
    ///     Dehydrates this scene node to a data transfer object. The hierarchy, which nests the children, is written
    ///     last, so a node's own components precede its descendants.
    /// </summary>
    /// <returns>A data transfer object containing the current state of this scene node.</returns>
    public SceneNodeData Dehydrate()
        => new()
        {
            Id = this.Id,
            Components =
            [
                .. this.Components.Where(component => component != this.Hierarchy).Select(static component => component.Dehydrate()),
                this.Hierarchy.Dehydrate(),
            ],
            OverrideSlots = [.. this.OverrideSlots.Select(static slot => slot.Dehydrate())],
        };

    private static bool IsRequired(GameComponent component)
        => component is MetadataComponent or HierarchyComponent or TransformComponent or NodeFlagsComponent;

    private static bool IsRequired(ComponentData data)
        => data is MetadataData or HierarchyData or TransformData or NodeFlagsData;

    private static void EnsureComponentIdentity(GameComponent component, HashSet<ComponentId> usedIds)
    {
        if (component.Id.IsEmpty || !usedIds.Add(component.Id))
        {
            do
            {
                component.Id = ComponentId.New();
            }
            while (!usedIds.Add(component.Id));
        }
    }

    private void HydrateOptionalComponents(IEnumerable<ComponentData> components)
    {
        var componentIds = new HashSet<ComponentId>();
        foreach (var required in this.Components)
        {
            EnsureComponentIdentity(required, componentIds);
        }

        foreach (var compData in components.Where(static data => !IsRequired(data)))
        {
            var component = GameComponent.CreateAndHydrate(compData);
            EnsureComponentIdentity(component, componentIds);
            component.Node = this;
            this.Components.Add(component);
        }
    }
}
