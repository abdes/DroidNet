// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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

    private const string OverrideSlots = nameof(GeometryComponentData.OverrideSlots) + "/*/";

    private const string TargetedOverrideSlots = nameof(GeometryComponentData.TargetedOverrides) + "/*/" + nameof(TargetedOverrideData.OverrideSlots) + "/*/";

    private const string ScriptSlots = nameof(ScriptingData.Slots) + "/*/";

    private const string Environment = nameof(SceneData.Environment) + "/";

    private const string References = nameof(SceneData.References) + "/";

    /// <summary>Gets the reference fields of a scene document.</summary>
    public static IReadOnlyList<AssetReferenceField> All { get; } =
    [
        new(Components + nameof(GeometryComponentData.GeometryUri)),
        new(Components + OverrideSlots + nameof(MaterialsSlotData.GeometryUri)),
        new(Components + OverrideSlots + nameof(MaterialsSlotData.MaterialUri)),
        new(Components + TargetedOverrideSlots + nameof(MaterialsSlotData.GeometryUri)),
        new(Components + TargetedOverrideSlots + nameof(MaterialsSlotData.MaterialUri)),
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
}
