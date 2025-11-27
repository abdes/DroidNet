// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Utils;

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
public partial class SceneNode : GameObject, IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private bool isActive;
    private bool isVisible = true;
    private bool castsShadows;
    private bool receivesShadows;
    private bool isRayCastingSelectable = true;
    private bool ignoreParentTransform;
    private bool isStatic;
    private bool isDisposed;

    // private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNode" /> class.
    /// </summary>
    /// <param name="scene">The scene associated with the scene node.</param>
    public SceneNode(Scene scene)
    {
        this.Scene = scene;

        // Initialize the components collection with the always-present Transform.
        // Use a concrete mutable collection that preserves insertion order.
        this.Components = [new Transform(this) { Name = nameof(Transform) }];
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

            _ = this.SetField(ref this.isActive, value);
        }
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether the node is visible (editor local value).
    /// </summary>
    public bool IsVisible
    {
        get => this.isVisible;
        set => _ = this.SetField(ref this.isVisible, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node casts shadows.
    /// </summary>
    public bool CastsShadows
    {
        get => this.castsShadows;
        set => _ = this.SetField(ref this.castsShadows, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node receives shadows.
    /// </summary>
    public bool ReceivesShadows
    {
        get => this.receivesShadows;
        set => _ = this.SetField(ref this.receivesShadows, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node is selectable by ray casting.
    /// </summary>
    public bool IsRayCastingSelectable
    {
        get => this.isRayCastingSelectable;
        set => _ = this.SetField(ref this.isRayCastingSelectable, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node ignores parent transforms.
    /// </summary>
    public bool IgnoreParentTransform
    {
        get => this.ignoreParentTransform;
        set => _ = this.SetField(ref this.ignoreParentTransform, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether gets or sets whether this node is considered static (editor hint).
    /// </summary>
    public bool IsStatic
    {
        get => this.isStatic;
        set => _ = this.SetField(ref this.isStatic, value);
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
    public ObservableCollection<SceneNode> Children { get; }

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

    /// <inheritdoc />
    public void Dispose()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Deserializes a JSON string into a <see cref="SceneNode" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <param name="scene">The scene to set in the deserialized <see cref="SceneNode" /> object.</param>
    /// <returns>The deserialized <see cref="SceneNode" /> object.</returns>
    /// <remarks>
    ///     This method uses the default <see cref="JsonSerializerOptions" /> defined in <see cref="JsonOptions" />.
    /// </remarks>
    [SuppressMessage(
        "Performance",
        "CA1869:Cache and reuse 'JsonSerializerOptions' instances",
        Justification = "we need to set the scene for the converter")]
    internal static SceneNode? FromJson(string json, Scene scene)
    {
        var options = new JsonSerializerOptions(JsonOptions) { Converters = { new SceneNodeJsonConverter(scene) } };
        return JsonSerializer.Deserialize<SceneNode>(json, options);
    }

    /// <summary>
    ///     Serializes a <see cref="SceneNode" /> object into a JSON string.
    /// </summary>
    /// <param name="sceneNode">The <see cref="SceneNode" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="SceneNode" /> object.</returns>
    /// <remarks>
    ///     This method uses the default <see cref="JsonSerializerOptions" /> defined in <see cref="JsonOptions" />.
    /// </remarks>
    [SuppressMessage(
        "Performance",
        "CA1869:Cache and reuse 'JsonSerializerOptions' instances",
        Justification = "we need to use the custom converter")]
    internal static string ToJson(SceneNode sceneNode)
    {
        var options = new JsonSerializerOptions(JsonOptions) { Converters = { new SceneNodeJsonConverter(null!) } };
        return JsonSerializer.Serialize(sceneNode, options);
    }

    /// <summary>
    ///     Disposes of the scene node.
    /// </summary>
    /// <param name="disposing">A value indicating whether the scene node is being disposed of deterministically.</param>
    /// <remarks>
    ///     The disposing parameter should be false when called from a finalizer, and true when called
    ///     from the IDisposable.Dispose method. In other words, it is true when deterministically
    ///     called and false when non-deterministically called.
    /// </remarks>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
        }

        // TODO: free unmanaged resources (unmanaged objects) and override finalizer
        // TODO: set large fields to null
        this.isDisposed = true;
    }
}
