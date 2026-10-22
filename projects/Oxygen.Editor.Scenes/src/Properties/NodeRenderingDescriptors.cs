// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// Descriptor catalog for authored node rendering flags; the edited target is the scene node itself.
/// </summary>
public sealed class NodeRenderingDescriptors
{
    private NodeRenderingDescriptors(
        PropertyDescriptor<bool> isVisible,
        PropertyDescriptor<bool> castsShadows,
        PropertyDescriptor<bool> receivesShadows)
    {
        this.IsVisibleDescriptor = isVisible;
        this.CastsShadowsDescriptor = castsShadows;
        this.ReceivesShadowsDescriptor = receivesShadows;
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [isVisible.Id] = isVisible,
            [castsShadows.Id] = castsShadows,
            [receivesShadows.Id] = receivesShadows,
        };
    }

    /// <summary>Gets the scene visibility descriptor.</summary>
    public PropertyDescriptor<bool> IsVisibleDescriptor { get; }

    /// <summary>Gets the shadow casting descriptor.</summary>
    public PropertyDescriptor<bool> CastsShadowsDescriptor { get; }

    /// <summary>Gets the shadow receiving descriptor.</summary>
    public PropertyDescriptor<bool> ReceivesShadowsDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Builds the canonical node rendering descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    public static NodeRenderingDescriptors Build()
        => new(
            Flag(SceneSchema.Node.IsVisible, "Scene Visibility", static node => node.IsVisible, static (node, value) => node.IsVisible = value),
            Flag(SceneSchema.Node.CastsShadows, "Cast Shadows", static node => node.CastsShadows, static (node, value) => node.CastsShadows = value),
            Flag(SceneSchema.Node.ReceivesShadows, "Receive Shadows", static node => node.ReceivesShadows, static (node, value) => node.ReceivesShadows = value));

    private static PropertyDescriptor<bool> Flag(
        PropertyId<bool> id,
        string label,
        Func<SceneNode, bool> read,
        Action<SceneNode, bool> write)
        => new(
            id: id,
            reader: target => read((SceneNode)target),
            writer: (target, value) => write((SceneNode)target, value),
            validator: static _ => ValidationResult.Ok,
            annotation: SceneEditorSchemaAnnotations.Get(
                "#/definitions/node_flags" + id.Id.JsonPointer,
                new EditorAnnotation { Group = "Rendering", Label = label, Renderer = "toggle" }));
}
