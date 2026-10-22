// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The required component holding a node's authored flags, the engine's <c>NodeData</c>: scene visibility, the
/// geometry's shadow casting and receiving, the static hint, ray-cast selection and parent-transform composition.
/// </summary>
public sealed class NodeFlagsComponent : GameComponent
{
    /// <summary>The component name a new node's flags take.</summary>
    public const string DefaultName = "Rendering";

    private bool isVisible = true;
    private bool castsShadows;
    private bool receivesShadows;
    private bool isStatic;
    private bool isRayCastingSelectable = true;
    private bool ignoreParentTransform;

    static NodeFlagsComponent()
    {
        Register<NodeFlagsData>(d =>
        {
            var flags = new NodeFlagsComponent { Name = d.Name };
            flags.Hydrate(d);
            return flags;
        });
    }

    /// <summary>
    /// Gets or sets a value indicating whether the node is visible.
    /// </summary>
    public bool IsVisible
    {
        get => this.isVisible;
        set => _ = this.SetProperty(ref this.isVisible, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the node's geometry casts shadows.
    /// </summary>
    public bool CastsShadows
    {
        get => this.castsShadows;
        set => _ = this.SetProperty(ref this.castsShadows, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the node's geometry receives shadows.
    /// </summary>
    public bool ReceivesShadows
    {
        get => this.receivesShadows;
        set => _ = this.SetProperty(ref this.receivesShadows, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the node is static, an optimization hint.
    /// </summary>
    public bool IsStatic
    {
        get => this.isStatic;
        set => _ = this.SetProperty(ref this.isStatic, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether ray casting can select the node.
    /// </summary>
    public bool IsRayCastingSelectable
    {
        get => this.isRayCastingSelectable;
        set => _ = this.SetProperty(ref this.isRayCastingSelectable, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the node's world transform ignores its parent's, so its local transform
    /// is its world one.
    /// </summary>
    public bool IgnoreParentTransform
    {
        get => this.ignoreParentTransform;
        set => _ = this.SetProperty(ref this.ignoreParentTransform, value);
    }

    /// <summary>
    /// Gets a value indicating whether the component can be removed; a required component cannot.
    /// </summary>
    public override bool IsLocked => true;

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);
        if (data is not NodeFlagsData d)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.IsVisible = d.IsVisible;
            this.CastsShadows = d.CastsShadows;
            this.ReceivesShadows = d.ReceivesShadows;
            this.IsStatic = d.IsStatic;
            this.IsRayCastingSelectable = d.IsRayCastingSelectable;
            this.IgnoreParentTransform = d.IgnoreParentTransform;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new NodeFlagsData
        {
            Id = this.Id,
            Name = this.Name,
            IsVisible = this.IsVisible,
            CastsShadows = this.CastsShadows,
            ReceivesShadows = this.ReceivesShadows,
            IsStatic = this.IsStatic,
            IsRayCastingSelectable = this.IsRayCastingSelectable,
            IgnoreParentTransform = this.IgnoreParentTransform,
        };
}
