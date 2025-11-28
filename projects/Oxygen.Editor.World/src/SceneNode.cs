// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
///     Represents a scene node with a name and an associated scene.
/// </summary>
/// <remarks>
///     The <see cref="SceneNode" /> class represents a scene node within a game, which is associated with a specific
///     scene.
///     It provides methods for JSON serialization and deserialization, allowing scene nodes to be easily saved and
///     loaded.
/// </remarks>
public partial class SceneNode : GameObject, IPersistent<Serialization.SceneNodeData>
{
    private bool isActive;
    private bool isVisible = true;
    private bool castsShadows;
    private bool receivesShadows;
    private bool isRayCastingSelectable = true;
    private bool ignoreParentTransform;
    private bool isStatic;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNode" /> class.
    /// </summary>
    /// <param name="scene">The scene associated with the scene node.</param>
    public SceneNode(Scene scene)
    {
        this.Scene = scene;
        this.Name = "New Node"; // Initialize required property

        // Initialize the components collection with the always-present Transform.
        // Use a concrete mutable collection that preserves insertion order.
        this.Components = [new Transform { Name = nameof(Transform), Node = this }];
        this.Children = [];
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the scene node is loaded in the Game Engine.
    /// </summary>
    public bool IsActive
    {
        get => this.isActive;
        set
        {
            if (this.isActive == value)
            {
                return;
            }

            _ = this.SetProperty(ref this.isActive, value);
        }
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether the node is visible (editor local value).
    /// </summary>
    public bool IsVisible
    {
        get => this.isVisible;
        set => _ = this.SetProperty(ref this.isVisible, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node casts shadows.
    /// </summary>
    public bool CastsShadows
    {
        get => this.castsShadows;
        set => _ = this.SetProperty(ref this.castsShadows, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node receives shadows.
    /// </summary>
    public bool ReceivesShadows
    {
        get => this.receivesShadows;
        set => _ = this.SetProperty(ref this.receivesShadows, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node is selectable by ray casting.
    /// </summary>
    public bool IsRayCastingSelectable
    {
        get => this.isRayCastingSelectable;
        set => _ = this.SetProperty(ref this.isRayCastingSelectable, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node ignores parent transforms.
    /// </summary>
    public bool IgnoreParentTransform
    {
        get => this.ignoreParentTransform;
        set => _ = this.SetProperty(ref this.ignoreParentTransform, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node is considered static (editor hint).
    /// </summary>
    public bool IsStatic
    {
        get => this.isStatic;
        set => _ = this.SetProperty(ref this.isStatic, value);
    }

    /// <summary>
    ///     Gets or sets a compact enum representing the current flags on this node.
    ///     The getter composes the enum from the individual observable properties and the
    ///     setter updates those properties (so change notifications fire).
    /// </summary>
    public SceneNodeFlags Flags
    {
        get
        {
            var f = SceneNodeFlags.None;
            if (this.IsVisible)
            {
                f |= SceneNodeFlags.Visible;
            }

            if (this.CastsShadows)
            {
                f |= SceneNodeFlags.CastsShadows;
            }

            if (this.ReceivesShadows)
            {
                f |= SceneNodeFlags.ReceivesShadows;
            }

            if (this.IsRayCastingSelectable)
            {
                f |= SceneNodeFlags.RayCastingSelectable;
            }

            if (this.IgnoreParentTransform)
            {
                f |= SceneNodeFlags.IgnoreParentTransform;
            }

            if (this.IsStatic)
            {
                f |= SceneNodeFlags.Static;
            }

            return f;
        }

        set
        {
            // Use property setters so change notifications fire.
            this.IsVisible = value.HasFlag(SceneNodeFlags.Visible);
            this.CastsShadows = value.HasFlag(SceneNodeFlags.CastsShadows);
            this.ReceivesShadows = value.HasFlag(SceneNodeFlags.ReceivesShadows);
            this.IsRayCastingSelectable = value.HasFlag(SceneNodeFlags.RayCastingSelectable);
            this.IgnoreParentTransform = value.HasFlag(SceneNodeFlags.IgnoreParentTransform);
            this.IsStatic = value.HasFlag(SceneNodeFlags.Static);
        }
    }

    /// <summary>
    ///     Gets the scene associated with the scene node.
    /// </summary>
    [JsonIgnore]
    public Scene Scene { get; }

    /// <summary>
    ///     Gets the list of components associated with the scene node.
    /// </summary>
    public ICollection<GameComponent> Components { get; private init; }

    /// <summary>
    ///     Gets the parent of the scene node.
    /// </summary>
    [JsonIgnore]
    public SceneNode? Parent { get; private set; }

    /// <summary>
    ///     Gets the collection of child nodes.
    /// </summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public ObservableCollection<SceneNode> Children { get; }

    /// <summary>
    /// Create and hydrate a <see cref="SceneNode"/> instance from DTO.
    /// Factory sets required properties (Name/Id) via object initializer and
    /// then calls the instance <see cref="Hydrate(Serialization.SceneNodeData)"/>.
    /// </summary>
    /// <param name="scene">The scene to associate with the new node.</param>
    /// <param name="data">The data transfer object to hydrate from.</param>
    /// <returns>A hydrated <see cref="SceneNode"/> instance.</returns>
    public static SceneNode CreateAndHydrate(Scene scene, Serialization.SceneNodeData data)
    {
        var node = new SceneNode(scene) { Name = data.Name, Id = data.Id };
        node.Hydrate(data);

        // If Children is null (missing from JSON), treat as empty
        if (data.Children is null)
        {
            node.Children.Clear();
        }

        return node;
    }

    /// <summary>
    /// Toggle a single flag on this node.
    /// </summary>
    /// <param name="flag">The flag to toggle.</param>
    public void ToggleFlag(SceneNodeFlags flag)
    {
        var f = this.Flags;
        this.Flags = f ^ flag;
    }

    /// <summary>
    /// Returns true if the node has the specified flag set.
    /// </summary>
    /// <param name="flag">The flag to check.</param>
    /// <returns>True if the flag is set on this node; otherwise false.</returns>
    public bool HasFlag(SceneNodeFlags flag) => this.Flags.HasFlag(flag);

    /// <summary>
    ///     Adds a child node to this node.
    /// </summary>
    /// <param name="child">The child node to add.</param>
    public void AddChild(SceneNode child)
    {
        ArgumentNullException.ThrowIfNull(child);
        child.SetParent(this);
    }

    /// <summary>
    ///     Removes a child node from this node.
    /// </summary>
    /// <param name="child">The child node to remove.</param>
    public void RemoveChild(SceneNode child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (child.Parent == this)
        {
            child.SetParent(newParent: null);
        }
    }

    /// <summary>
    ///     Sets the parent of this node.
    /// </summary>
    /// <param name="newParent">The new parent node, or null to detach.</param>
    public void SetParent(SceneNode? newParent)
    {
        if (this.Parent == newParent)
        {
            return;
        }

        if (newParent != null && (newParent == this || newParent.Ancestors().Contains(this)))
        {
            throw new InvalidOperationException("Cannot set parent: would create circular reference.");
        }

        var oldParent = this.Parent;
        this.Parent = newParent;

        _ = oldParent?.Children.Remove(this);

        if (newParent?.Children.Contains(this) == false)
        {
            newParent.Children.Add(this);
        }
    }

    /// <summary>
    ///     Gets all descendant nodes.
    /// </summary>
    /// <returns>An enumerable of descendant nodes.</returns>
    public IEnumerable<SceneNode> Descendants()
    {
        foreach (var child in this.Children)
        {
            yield return child;
            foreach (var descendant in child.Descendants())
            {
                yield return descendant;
            }
        }
    }

    /// <summary>
    ///     Gets all ancestor nodes.
    /// </summary>
    /// <returns>An enumerable of ancestor nodes.</returns>
    public IEnumerable<SceneNode> Ancestors()
    {
        var current = this.Parent;
        while (current != null)
        {
            yield return current;
            current = current.Parent;
        }
    }

    /// <summary>
    ///     Hydrates this scene node instance from the specified DTO. The factory
    ///     `CreateAndHydrate` is responsible for setting required properties (Name/Id).
    /// </summary>
    /// <param name="data">The node DTO to hydrate from.</param>
    public void Hydrate(Serialization.SceneNodeData data)
    {
        using (this.SuppressNotifications())
        {
            // 1. Recreate components from DTO using the canonical factory API.
            // Clear existing components and recreate all components so component
            // creation/hydration follows the same path for every component type.
            this.Components.Clear();

            foreach (var compData in data.Components)
            {
                var component = GameComponent.CreateAndHydrate(compData);
                component.Node = this;
                this.Components.Add(component);
            }

            // 2. Ensure exactly one Transform component exists after hydration.
            var transforms = this.Components.OfType<Transform>().ToList();
            if (transforms.Count == 0)
            {
                throw new System.Text.Json.JsonException("Missing required Transform component in SceneNode data.");
            }

            if (transforms.Count > 1)
            {
                throw new System.Text.Json.JsonException("Multiple Transform components found in SceneNode data.");
            }

            // 3. Component & node-level override slots
            // Node-level override slots are persisted separately from components.
            this.OverrideSlots.Clear();
            foreach (var slotData in data.OverrideSlots)
            {
                var slot = Slots.OverrideSlot.CreateAndHydrate(slotData);
                this.OverrideSlots.Add(slot);
            }

            // 4. Flags
            this.IsActive = data.IsActive;
            this.IsVisible = data.IsVisible;
            this.CastsShadows = data.CastsShadows;
            this.ReceivesShadows = data.ReceivesShadows;
            this.IsRayCastingSelectable = data.IsRayCastingSelectable;
            this.IgnoreParentTransform = data.IgnoreParentTransform;
            this.IsStatic = data.IsStatic;

            // 5. Children
            if (data.Children != null)
            {
                foreach (var childData in data.Children)
                {
                    var child = CreateAndHydrate(this.Scene, childData);
                    this.AddChild(child);
                }
            }
        }
    }

    /// <summary>
    ///     Dehydrates this scene node to a data transfer object.
    /// </summary>
    /// <returns>A data transfer object containing the current state of this scene node.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0305:Simplify collection initialization", Justification = "With LINQ, /ToList() is more natural")]
    public Serialization.SceneNodeData Dehydrate()
    {
        var transform = this.Components.OfType<Transform>().FirstOrDefault();
        return new Serialization.SceneNodeData
        {
            Name = this.Name,
            Id = this.Id,
            Components = this.Components.Select(c => c.Dehydrate()).ToList(),
            OverrideSlots = this.OverrideSlots.Select(s => s.Dehydrate()).ToList(),
            Children = this.Children.Select(c => c.Dehydrate()).ToList(),
            IsActive = this.IsActive,
            IsVisible = this.IsVisible,
            CastsShadows = this.CastsShadows,
            ReceivesShadows = this.ReceivesShadows,
            IsRayCastingSelectable = this.IsRayCastingSelectable,
            IgnoreParentTransform = this.IgnoreParentTransform,
            IsStatic = this.IsStatic,
        };
    }
}
