// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for authored node flags; the edited target is the node's <see cref="NodeFlagsComponent"/>.
/// </summary>
public sealed class NodeFlagsDescriptors
{
    private NodeFlagsDescriptors(
        PropertyDescriptor<bool> isVisible,
        PropertyDescriptor<bool> castsShadows,
        PropertyDescriptor<bool> receivesShadows,
        PropertyDescriptor<bool> isStatic,
        PropertyDescriptor<bool> isRayCastingSelectable,
        PropertyDescriptor<bool> ignoreParentTransform)
    {
        this.IsVisibleDescriptor = isVisible;
        this.CastsShadowsDescriptor = castsShadows;
        this.ReceivesShadowsDescriptor = receivesShadows;
        this.IsStaticDescriptor = isStatic;
        this.IsRayCastingSelectableDescriptor = isRayCastingSelectable;
        this.IgnoreParentTransformDescriptor = ignoreParentTransform;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [isVisible.Id] = isVisible,
            [castsShadows.Id] = castsShadows,
            [receivesShadows.Id] = receivesShadows,
            [isStatic.Id] = isStatic,
            [isRayCastingSelectable.Id] = isRayCastingSelectable,
            [ignoreParentTransform.Id] = ignoreParentTransform,
        };
    }

    /// <summary>Gets the scene visibility descriptor.</summary>
    public PropertyDescriptor<bool> IsVisibleDescriptor { get; }

    /// <summary>Gets the shadow casting descriptor.</summary>
    public PropertyDescriptor<bool> CastsShadowsDescriptor { get; }

    /// <summary>Gets the shadow receiving descriptor.</summary>
    public PropertyDescriptor<bool> ReceivesShadowsDescriptor { get; }

    /// <summary>Gets the static hint descriptor.</summary>
    public PropertyDescriptor<bool> IsStaticDescriptor { get; }

    /// <summary>Gets the ray-cast selection descriptor.</summary>
    public PropertyDescriptor<bool> IsRayCastingSelectableDescriptor { get; }

    /// <summary>Gets the parent-transform composition descriptor.</summary>
    public PropertyDescriptor<bool> IgnoreParentTransformDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Builds the canonical node-flags descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    public static NodeFlagsDescriptors Build()
        => new(
            Flag(SceneSchema.Node.IsVisible, "Scene Visibility", static flags => flags.IsVisible, static (flags, value) => flags.IsVisible = value),
            Flag(SceneSchema.Node.CastsShadows, "Cast Shadows", static flags => flags.CastsShadows, static (flags, value) => flags.CastsShadows = value),
            Flag(SceneSchema.Node.ReceivesShadows, "Receive Shadows", static flags => flags.ReceivesShadows, static (flags, value) => flags.ReceivesShadows = value),
            Flag(SceneSchema.Node.IsStatic, "Static", static flags => flags.IsStatic, static (flags, value) => flags.IsStatic = value),
            Flag(SceneSchema.Node.IsRayCastingSelectable, "Ray-Cast Selectable", static flags => flags.IsRayCastingSelectable, static (flags, value) => flags.IsRayCastingSelectable = value),
            Flag(SceneSchema.Node.IgnoreParentTransform, "Ignore Parent Transform", static flags => flags.IgnoreParentTransform, static (flags, value) => flags.IgnoreParentTransform = value));

    private static PropertyDescriptor<bool> Flag(
        PropertyId<bool> id,
        string label,
        Func<NodeFlagsComponent, bool> read,
        Action<NodeFlagsComponent, bool> write)
        => new(
            id: id,
            reader: target => read((NodeFlagsComponent)target),
            writer: (target, value) => write((NodeFlagsComponent)target, value),
            validator: static _ => ValidationResult.Ok,
            annotation: SceneEditorSchemaAnnotations.Get(
                "#/definitions/node_flags" + id.Id.JsonPointer,
                new EditorAnnotation { Group = "Rendering", Label = label, Renderer = "toggle" }));
}
