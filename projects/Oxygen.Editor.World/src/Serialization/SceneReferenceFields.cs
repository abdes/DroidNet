// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// The fields of a scene document that reference assets, named after the serialized properties, so that tools such as
/// relocation rewrite a scene's references and leave its text alone.
/// </summary>
public static class SceneReferenceFields
{
    // Every component of every node, at any depth of the node tree.
    private const string Components = nameof(SceneData.RootNodes) + "/**/" + nameof(SceneNodeData.Components) + "/*/";

    private const string Overrides = nameof(IOverridableComponent.Overrides) + "/";

    private const string ScriptSlots = nameof(ScriptingData.Slots) + "/*/";

    private const string Environment = nameof(SceneData.Environment) + "/";

    private const string References = nameof(SceneData.References) + "/";

    /// <summary>Gets the reference fields of a scene document.</summary>
    public static IReadOnlyList<AssetReferenceField> All { get; } =
    [
        new(Components + nameof(GeometryComponentData.GeometryUri)),
        .. OverrideFields(),
        new(Components + nameof(SceneInstanceComponentData.Source)),
        .. InstanceChangeFields(),
        new(Components + nameof(SceneInstanceComponentData.Changes) + "/*/" + nameof(InstancedNodeChangesData.Parameters) + "/*/*/" + nameof(ScriptValueData.Value))
        {
            WhenProperty = nameof(ScriptValueData.Type),
            WhenValue = ScriptValueData.AssetType,
        },
        new(Components + ScriptSlots + nameof(ScriptSlotData.Script)),
        new(Components + ScriptSlots + nameof(ScriptSlotData.Overrides) + "/*/" + nameof(ScriptValueData.Value))
        {
            WhenProperty = nameof(ScriptValueData.Type),
            WhenValue = ScriptValueData.AssetType,
        },
        new(Environment + nameof(SceneEnvironmentData.SkySphere) + "/" + nameof(SkySphereEnvironmentData.Cubemap)),
        new(Environment + nameof(SceneEnvironmentData.SkyLight) + "/" + nameof(SkyLightEnvironmentData.Cubemap)),
        new(Environment + nameof(SceneEnvironmentData.PostProcess) + "/" + nameof(PostProcessEnvironmentData.AutoExposureMeteringMask)),
        new(References + nameof(SceneReferencesData.Scripts) + "/*"),
        new(References + nameof(SceneReferencesData.InputActions) + "/*"),
        new(References + nameof(SceneReferencesData.PhysicsSidecars) + "/*"),
        new(References + nameof(SceneReferencesData.ExtraAssets) + "/*"),
        new(nameof(SceneData.StartingInputContexts) + "/*/" + nameof(StartingInputContextData.Context)),
    ];

    // One field per asset-typed property an instance may change, under its component kind, as for overrides.
    private static IEnumerable<AssetReferenceField> InstanceChangeFields()
        => SceneSchema.Changeable
            .Where(static entry => entry.Value == typeof(AssetPath))
            .Select(static entry => entry.Key)
            .OrderBy(static property => property.Qualified(), StringComparer.Ordinal)
            .Select(static property => new AssetReferenceField(
                Components + nameof(SceneInstanceComponentData.Changes) + "/*/" + nameof(InstancedNodeChangesData.Components)
                + "/" + AssetReferenceField.Escape(property.ComponentKind) + "/"
                + (property.PartKind is null ? string.Empty : nameof(SourceOverridesData.Parts) + "/*/")
                + nameof(SourceOverridesData.Values) + "/" + AssetReferenceField.Escape(property.JsonPointer)));

    // One field per asset-typed overridable property, so that a string value of another property is never taken for
    // an asset reference. The property's key is a JSON pointer, escaped as one path segment.
    private static IEnumerable<AssetReferenceField> OverrideFields()
        => SceneSchema.Overridable
            .Where(static entry => entry.Value == typeof(AssetPath))
            .Select(static entry => entry.Key)
            .OrderBy(static property => property.Qualified(), StringComparer.Ordinal)
            .Select(static property => new AssetReferenceField(
                Components + Overrides
                + (property.PartKind is null ? string.Empty : nameof(SourceOverridesData.Parts) + "/*/")
                + nameof(SourceOverridesData.Values) + "/" + AssetReferenceField.Escape(property.JsonPointer)));
}
