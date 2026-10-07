// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Descriptor catalog for authored node rendering flags; the edited target is the scene node itself.
/// </summary>
internal sealed class NodeRenderingDescriptors
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

    /// <summary>Gets the typed property id for scene visibility.</summary>
    internal PropertyId<bool> IsVisible => new(this.IsVisibleDescriptor.Id);

    /// <summary>Gets the typed property id for geometry shadow casting.</summary>
    internal PropertyId<bool> CastsShadows => new(this.CastsShadowsDescriptor.Id);

    /// <summary>Gets the typed property id for geometry shadow receiving.</summary>
    internal PropertyId<bool> ReceivesShadows => new(this.ReceivesShadowsDescriptor.Id);

    /// <summary>Gets the scene visibility descriptor.</summary>
    internal PropertyDescriptor<bool> IsVisibleDescriptor { get; }

    /// <summary>Gets the shadow casting descriptor.</summary>
    internal PropertyDescriptor<bool> CastsShadowsDescriptor { get; }

    /// <summary>Gets the shadow receiving descriptor.</summary>
    internal PropertyDescriptor<bool> ReceivesShadowsDescriptor { get; }

    /// <summary>Gets descriptors indexed by property id.</summary>
    internal IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>Builds the canonical node rendering descriptor catalog.</summary>
    /// <returns>The descriptor catalog.</returns>
    internal static NodeRenderingDescriptors Build()
        => new(
            Flag("/visible", "Scene Visibility", static node => node.IsVisible, static (node, value) => node.IsVisible = value),
            Flag("/casts_shadows", "Cast Shadows", static node => node.CastsShadows, static (node, value) => node.CastsShadows = value),
            Flag("/receives_shadows", "Receive Shadows", static node => node.ReceivesShadows, static (node, value) => node.ReceivesShadows = value));

    private static PropertyDescriptor<bool> Flag(
        string pointer,
        string label,
        Func<SceneNode, bool> read,
        Action<SceneNode, bool> write)
        => new(
            id: new PropertyId<bool>(SceneDocumentCommandService.NodeRenderingKind, pointer),
            reader: target => read((SceneNode)target),
            writer: (target, value) => write((SceneNode)target, value),
            validator: static _ => ValidationResult.Ok,
            annotation: SceneEditorSchemaAnnotations.Get(
                "#/definitions/node_flags" + pointer,
                new EditorAnnotation { Group = "Rendering", Label = label, Renderer = "toggle" }),
            engineCommandKey: "node" + pointer.Replace('/', '.'));
}
