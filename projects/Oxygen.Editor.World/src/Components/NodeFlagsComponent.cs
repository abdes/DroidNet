// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The required component holding a node's authored flags, the engine's <c>NodeData</c>: scene visibility, the
/// geometry's shadow casting and receiving, the static hint, ray-cast selection and parent-transform composition.
/// </summary>
/// <remarks>
/// Scene visibility and the two shadow flags store a source mode: Inherit takes the parent's resolved value, and a
/// root's Inherit takes the scene defaults Shown, On and On. The resolution methods give the value and the node it
/// comes from; resolved values are never stored.
/// </remarks>
public sealed class NodeFlagsComponent : GameComponent
{
    /// <summary>The component name a new node's flags take.</summary>
    public const string DefaultName = "Rendering";

    private VisibilitySource visibility;
    private ShadowSource castsShadows;
    private ShadowSource receivesShadows;
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
    /// Gets or sets the source mode of the node's scene visibility.
    /// </summary>
    public VisibilitySource Visibility
    {
        get => this.visibility;
        set => _ = this.SetProperty(ref this.visibility, value);
    }

    /// <summary>
    /// Gets or sets the source mode of the node's geometry shadow casting.
    /// </summary>
    public ShadowSource CastsShadows
    {
        get => this.castsShadows;
        set => _ = this.SetProperty(ref this.castsShadows, value);
    }

    /// <summary>
    /// Gets or sets the source mode of the node's geometry shadow receiving.
    /// </summary>
    public ShadowSource ReceivesShadows
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

    /// <summary>Resolves the node's scene visibility.</summary>
    /// <returns>Whether the node is shown, and the node whose local mode decides it.</returns>
    public ResolvedNodeFlag ResolveVisibility()
        => this.Resolve(static flags => flags.Visibility switch
        {
            VisibilitySource.Shown => true,
            VisibilitySource.Hidden => false,
            _ => null,
        });

    /// <summary>Resolves whether the node's geometry casts shadows.</summary>
    /// <returns>Whether it casts, and the node whose local mode decides it.</returns>
    public ResolvedNodeFlag ResolveCastsShadows() => this.Resolve(static flags => Local(flags.CastsShadows));

    /// <summary>Resolves whether the node's geometry receives shadows.</summary>
    /// <returns>Whether it receives, and the node whose local mode decides it.</returns>
    public ResolvedNodeFlag ResolveReceivesShadows() => this.Resolve(static flags => Local(flags.ReceivesShadows));

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
            this.Visibility = d.Visibility;
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
            Visibility = this.Visibility,
            CastsShadows = this.CastsShadows,
            ReceivesShadows = this.ReceivesShadows,
            IsStatic = this.IsStatic,
            IsRayCastingSelectable = this.IsRayCastingSelectable,
            IgnoreParentTransform = this.IgnoreParentTransform,
        };

    private static bool? Local(ShadowSource source) => source switch
    {
        ShadowSource.On => true,
        ShadowSource.Off => false,
        _ => null,
    };

    // Inherit copies the parent's resolved value, up to a local mode or, past the root, the scene default (true for
    // all three flags).
    private ResolvedNodeFlag Resolve(Func<NodeFlagsComponent, bool?> local)
    {
        if (local(this) is { } own)
        {
            return new ResolvedNodeFlag(own, this.Node);
        }

        for (var node = this.Node?.Hierarchy.Parent; node is not null; node = node.Hierarchy.Parent)
        {
            if (local(node.Flags) is { } value)
            {
                return new ResolvedNodeFlag(value, node);
            }
        }

        return new ResolvedNodeFlag(Value: true, Source: null);
    }
}
