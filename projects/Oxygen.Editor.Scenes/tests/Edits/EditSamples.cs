// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Edits.EditorView;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Edits.Scripting;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits;

/// <summary>
/// One sample per public <see cref="ISceneEdit"/> type of the Scenes assembly, plus a batch and a nested batch, all over
/// <see cref="SampleScene"/>. <see cref="EditContractTests"/> runs every obligation of an edit over these rows.
/// </summary>
/// <remarks>
/// Each sample changes the document (or, for <see cref="SetEditorHidden"/>, the hidden nodes). The ids of the nodes,
/// components and folders the samples create are fixed here, so a sample builds the same edit every time.
/// </remarks>
public static class EditSamples
{
    /// <summary>Gets the node <see cref="InsertNodes"/> inserts.</summary>
    public static NodeId InsertedNode { get; } = NodeId.FromValue(SampleScene.Tagged(1, "1A"));

    /// <summary>Gets every sample, one row per edit type, display name = the edit type.</summary>
    public static IEnumerable<TestDataRow<EditSample>> All => Rows(Samples);

    /// <summary>Gets the samples whose edit has targets.</summary>
    public static IEnumerable<TestDataRow<EditSample>> WithTargets => Rows(Samples.Where(static sample => sample.HasTargets));

    /// <summary>
    /// Gets the samples whose first target has a parent node that is not itself a target, so locking the parent locks
    /// the first target alone.
    /// </summary>
    public static IEnumerable<TestDataRow<EditSample>> WithNestedTarget
        => Rows(Samples.Where(static sample => sample.HasTargets && ParentOfFirstTarget(sample) is { } parent && !sample.Build(SampleScene.Build()).Targets.Contains(parent)));

    /// <summary>Gets the samples that write an asset reference.</summary>
    public static IEnumerable<TestDataRow<EditSample>> WithAssetReference => Rows(Samples.Where(static sample => sample.CarriesAssetReference));

    /// <summary>Gets the samples built from serialization records or property edits the test can change afterwards.</summary>
    public static IEnumerable<TestDataRow<EditSample>> OwningItsInput => Rows(Samples.Where(static sample => sample.Tamper is not null));

    private static ComponentId InsertedGeometry { get; } = ComponentId.FromValue(SampleScene.Tagged(2, "1A"));

    private static ComponentId AddedCamera { get; } = ComponentId.FromValue(SampleScene.Tagged(2, "3A"));

    private static ComponentId AddedGeometry { get; } = ComponentId.FromValue(SampleScene.Tagged(2, "3B"));

    private static ComponentId RestoredInstance { get; } = ComponentId.FromValue(SampleScene.Tagged(2, "3C"));

    private static NodeId UnpackedBody { get; } = NodeId.FromValue(SampleScene.Tagged(1, "1B"));

    private static NodePath CarBody { get; } = NodePath.Of(NodeId.FromValue(SampleScene.Tagged(1, "1D")));

    private static FolderId NewFolder { get; } = FolderId.FromValue(SampleScene.Tagged(3, "2A"));

    private static FolderId RestoredFolder { get; } = FolderId.FromValue(SampleScene.Tagged(3, "2B"));

    private static ScriptSlotId AddedSlot { get; } = ScriptSlotId.FromValue(SampleScene.Tagged(4, "C3"));

    private static AssetPath GreenMaterial { get; } = AssetPath.Parse("/Content/Green.omat");

    private static AssetPath MaskTexture { get; } = AssetPath.Parse("/Content/Mask.otex");

    private static AssetPath SpinScript { get; } = AssetPath.Parse("/Content/Spin.oscript");

    private static AssetPath JumpScript { get; } = AssetPath.Parse("/Content/Jump.oscript");

    private static AssetPath VanModel { get; } = AssetPath.Parse("/Content/Vehicles/Van.glb");

    private static SurfaceTarget CubeSurfaceTarget => new(SampleScene.CubeGeometry, SampleScene.CubeSurface, SampleScene.CubeLayoutRevision);

    private static IReadOnlyList<EditSample> Samples =>
    [
        InsertNodesSample,
        RemoveNodesSample,
        RenameNodeSample,
        ReparentNodesSample,
        ReorderNodeSample,
        PlaceNodesSample,
        CreateFolderSample,
        RemoveFolderSample,
        RestoreFolderSample,
        RenameFolderSample,
        MoveLayoutEntriesSample,
        PlaceLayoutEntriesSample,
        AddComponentSample,
        RemoveComponentSample,
        SetPropertiesSample,
        SetScenePropertiesSample,
        SetSceneEnvironmentSample,
        SetGeometrySample,
        SetOverridesSample,
        ResetOverridesSample,
        SetStartingCameraSample,
        SetInstanceSourceSample,
        UnpackInstanceSample,
        RestoreInstanceSample,
        ApplyToSourceSample,
        RestoreSourceSample,
        SetStartingInputContextsSample,
        SetSceneReferencesSample,
        SetScriptSlotsSample,
        AddScriptSlotSample,
        RemoveScriptSlotSample,
        MoveScriptSlotSample,
        SetSlotScriptSample,
        SetSlotExecutionOrderSample,
        SetScriptValueSample,
        ResetScriptValueSample,
        RemoveStaleScriptValuesSample,
        SetEditorHiddenSample,
        BatchSample,
        NestedBatchSample,
    ];

    private static EditSample InsertNodesSample =>
        new(
            nameof(InsertNodes),
            static _ => new InsertNodes([InsertAtA2(InsertedNodeData())]),
            [new SceneChange.NodesAdded([InsertedNode]), new SceneChange.LayoutChanged()],
            [new SceneChange.NodesRemoved([InsertedNode]), new SceneChange.LayoutChanged()])
        {
            HasTargets = true,
            CarriesAssetReference = true,
            Asset = SampleScene.SphereGeometry,
            AssetNode = InsertedNode,
            Tamper = static _ =>
            {
                var node = InsertedNodeData();
                var inserts = new List<NodeInsert> { InsertAtA2(node) };
                return new TamperedEdit(
                    new InsertNodes(inserts),
                    () =>
                    {
                        node.Components.Clear();
                        inserts.Clear();
                    });
            },
        };

    private static EditSample RemoveNodesSample =>
        new(
            nameof(RemoveNodes),
            static _ => new RemoveNodes([SampleScene.A2]),
            [new SceneChange.NodesRemoved([SampleScene.A2]), new SceneChange.LayoutChanged()],
            [new SceneChange.NodesAdded([SampleScene.A2]), new SceneChange.LayoutChanged()])
        {
            HasTargets = true,
        };

    private static EditSample RenameNodeSample =>
        new(
            nameof(RenameNode),
            static _ => new RenameNode(SampleScene.A11, "Renamed"),
            [new SceneChange.NodeRenamed(SampleScene.A11)],
            [new SceneChange.NodeRenamed(SampleScene.A11)])
        {
            HasTargets = true,
        };

    private static EditSample ReparentNodesSample =>
        new(
            nameof(ReparentNodes),
            static _ => new ReparentNodes([SampleScene.A11], SampleScene.B, preserveWorld: true),
            [new SceneChange.NodesPlaced([SampleScene.A11]), new SceneChange.LayoutChanged()],
            [new SceneChange.NodesPlaced([SampleScene.A11]), new SceneChange.LayoutChanged()])
        {
            HasTargets = true,
        };

    private static EditSample ReorderNodeSample =>
        new(
            nameof(ReorderNode),
            static _ => new ReorderNode(SampleScene.B, new NodePosition(Parent: null, Index: 2), LayoutPosition.InFolder(SampleScene.F, 1)),
            [new SceneChange.NodesPlaced([SampleScene.B]), new SceneChange.LayoutChanged()],
            [new SceneChange.NodesPlaced([SampleScene.B]), new SceneChange.LayoutChanged()])
        {
            HasTargets = true,
        };

    private static EditSample PlaceNodesSample =>
        new(
            nameof(PlaceNodes),
            static _ => new PlaceNodes(
            [
                new NodePlacementState(
                    SampleScene.A2,
                    new NodePosition(SampleScene.A, Index: 0),
                    SampleScene.Transform(position: new Vector3(0, 2, 0)),
                    LayoutPosition.InFolder(SampleScene.AF, 0)),
            ]),
            [new SceneChange.NodesPlaced([SampleScene.A2]), new SceneChange.LayoutChanged()],
            [new SceneChange.NodesPlaced([SampleScene.A2]), new SceneChange.LayoutChanged()])
        {
            HasTargets = true,
        };

    private static EditSample CreateFolderSample =>
        new(
            nameof(CreateFolder),
            static _ => new CreateFolder(NewFolder, LayoutPosition.UnderNode(SampleScene.A1, 0), "New"),
            [new SceneChange.LayoutChanged()],
            [new SceneChange.LayoutChanged()])
        {
            HasTargets = true,
        };

    private static EditSample RemoveFolderSample =>
        new(
            nameof(RemoveFolder),
            static _ => new RemoveFolder(SampleScene.AF, PromoteChildren: true),
            [new SceneChange.LayoutChanged()],
            [new SceneChange.LayoutChanged()]);

    private static EditSample RestoreFolderSample =>
        new(
            nameof(RestoreFolder),
            static _ => RestoringFolder(RestoredChildren(), RestoredFolderEntry("Restored")),
            [new SceneChange.LayoutChanged()],
            [new SceneChange.LayoutChanged()])
        {
            Tamper = static _ =>
            {
                var entry = RestoredFolderEntry("Restored");
                var children = RestoredChildren();
                return new TamperedEdit(
                    RestoringFolder(children, entry),
                    () =>
                    {
                        entry.Name = "Mutated";
                        entry.IsExpanded = false;
                        entry.EnsureChildren().Add(new ExplorerEntryData { NodeId = SampleScene.B });
                        children.Clear();
                    });
            },
        };

    private static EditSample RenameFolderSample =>
        new(
            nameof(RenameFolder),
            static _ => new RenameFolder(SampleScene.AF, "Renamed"),
            [new SceneChange.LayoutChanged()],
            [new SceneChange.LayoutChanged()]);

    private static EditSample MoveLayoutEntriesSample =>
        new(
            nameof(MoveLayoutEntries),
            static _ => new MoveLayoutEntries([LayoutEntryId.Of(SampleScene.B)], LayoutPosition.InFolder(SampleScene.G, 0)),
            [new SceneChange.LayoutChanged()],
            [new SceneChange.LayoutChanged()]);

    private static EditSample PlaceLayoutEntriesSample =>
        new(
            nameof(PlaceLayoutEntries),
            static _ => new PlaceLayoutEntries([new LayoutEntryPosition(LayoutEntryId.Of(SampleScene.Cam), LayoutPosition.InFolder(SampleScene.G, 0))]),
            [new SceneChange.LayoutChanged()],
            [new SceneChange.LayoutChanged()]);

    private static EditSample AddComponentSample =>
        new(
            nameof(AddComponent),
            static _ => new AddComponent(SampleScene.A11, CameraToAdd(), index: SceneBuilder.RequiredComponentCount),
            [new SceneChange.ComponentAdded(SampleScene.A11, AddedCamera)],
            [new SceneChange.ComponentRemoved(SampleScene.A11, AddedCamera, SceneComponents.PerspectiveCamera)])
        {
            HasTargets = true,
            Tamper = static _ =>
            {
                var parts = new List<PartOverridesData>();
                var component = new GeometryComponentData
                {
                    Id = AddedGeometry,
                    Name = "Geometry",
                    GeometryUri = SampleScene.SphereGeometry,
                    Overrides = new SourceOverridesData { Parts = parts },
                };
                return new TamperedEdit(
                    new AddComponent(SampleScene.A11, component, index: SceneBuilder.RequiredComponentCount),
                    () => parts.Add(new PartOverridesData
                    {
                        Kind = SceneSchema.Geometry.Surface.Kind,
                        Id = SampleScene.CubeSurface,
                        LayoutRevision = SampleScene.CubeLayoutRevision,
                        Values = new Dictionary<string, JsonElement>(StringComparer.Ordinal)
                        {
                            [SceneSchema.Geometry.Surface.Material.Id.JsonPointer] = JsonSerializer.SerializeToElement(SampleScene.RedMaterial, SceneJsonContext.Default.AssetPath),
                        },
                    }));
            },
        };

    private static EditSample RemoveComponentSample =>
        new(
            nameof(RemoveComponent),
            static _ => new RemoveComponent(SampleScene.Lamp, SampleScene.LampLight),
            [new SceneChange.ComponentRemoved(SampleScene.Lamp, SampleScene.LampLight, SceneComponents.PointLight)],
            [new SceneChange.ComponentAdded(SampleScene.Lamp, SampleScene.LampLight)])
        {
            HasTargets = true,
        };

    private static EditSample SetPropertiesSample =>
        new(
            nameof(SetProperties),
            static _ => MovingA1(),
            [new SceneChange.PropertiesChanged(SampleScene.A1, [SceneSchema.Transform.PositionX.Id], EditPhase.Commit)],
            [new SceneChange.PropertiesChanged(SampleScene.A1, [SceneSchema.Transform.PositionX.Id], EditPhase.Commit)])
        {
            HasTargets = true,
            Tamper = static _ =>
            {
                var edits = new Dictionary<NodeId, PropertyEdit> { [SampleScene.A1] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionX, 5f) };
                return new TamperedEdit(new SetProperties(edits, "Move"), edits.Clear);
            },
        };

    private static EditSample SetScenePropertiesSample =>
        new(
            nameof(SetSceneProperties),
            static _ => new SetSceneProperties(PropertyEdit.SingleEdit<AssetPath?>(SceneProperties.SceneEnvironment.AutoExposureMeteringMask, MaskTexture), "Set mask"),
            [new SceneChange.PropertiesChanged(Node: null, [SceneProperties.SceneEnvironment.AutoExposureMeteringMask.Id], EditPhase.Commit)],
            [new SceneChange.PropertiesChanged(Node: null, [SceneProperties.SceneEnvironment.AutoExposureMeteringMask.Id], EditPhase.Commit)])
        {
            CarriesAssetReference = true,
            Asset = MaskTexture,
            Tamper = static _ =>
            {
                var builder = new PropertyEdit.Builder().Set(SceneProperties.SceneEnvironment.ExposureCompensation, 1f);
                return new TamperedEdit(
                    new SetSceneProperties(builder.Build(), "Compensate"),
                    () => builder.Set(SceneProperties.SceneEnvironment.ExposureCompensation, 2f));
            },
        };

    private static EditSample SetSceneEnvironmentSample =>
        new(
            nameof(SetSceneEnvironment),
            static data => new SetSceneEnvironment(
                data.Environment with { PostProcess = data.Environment.PostProcess with { ExposureCompensationEv = data.Environment.PostProcess.ExposureCompensationEv + 1f } },
                "Set environment"),
            [new SceneChange.PropertiesChanged(Node: null, [SceneProperties.SceneEnvironment.ExposureCompensation.Id], EditPhase.Commit)],
            [new SceneChange.PropertiesChanged(Node: null, [SceneProperties.SceneEnvironment.ExposureCompensation.Id], EditPhase.Commit)]);

    private static EditSample SetGeometrySample =>
        new(
            nameof(SetGeometry),
            static _ => new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry),
            [new SceneChange.GeometryChanged(SampleScene.Mesh)],
            [new SceneChange.GeometryChanged(SampleScene.Mesh)])
        {
            HasTargets = true,
            CarriesAssetReference = true,
            Asset = SampleScene.SphereGeometry,
            AssetNode = SampleScene.Mesh,
        };

    private static EditSample SetOverridesSample =>
        new(
            nameof(SetOverrides),
            static _ => SurfaceMaterial(GreenMaterial),
            [new SceneChange.OverridesChanged(SampleScene.Mesh, [SceneSchema.Geometry.Surface.Material.Id])],
            [new SceneChange.OverridesChanged(SampleScene.Mesh, [SceneSchema.Geometry.Surface.Material.Id])])
        {
            HasTargets = true,
            CarriesAssetReference = true,
            Asset = GreenMaterial,
            AssetNode = SampleScene.Mesh,
        };

    private static EditSample ResetOverridesSample =>
        new(
            nameof(SetOverrides) + " reset",
            static _ => SurfaceMaterial(material: null),
            [new SceneChange.OverridesChanged(SampleScene.Mesh, [SceneSchema.Geometry.Surface.Material.Id])],
            [new SceneChange.OverridesChanged(SampleScene.Mesh, [SceneSchema.Geometry.Surface.Material.Id])])
        {
            HasTargets = true,
        };

    private static EditSample SetInstanceSourceSample =>
        new(
            nameof(SetInstanceSource),
            static _ => new SetInstanceSource(SampleScene.Car, VanModel, InstanceSwap.KeepByName),
            [new SceneChange.InstanceSourceChanged(SampleScene.Car)],
            [new SceneChange.InstanceSourceChanged(SampleScene.Car)])
        {
            HasTargets = true,
            CarriesAssetReference = true,
            Asset = VanModel,
            AssetNode = SampleScene.Car,
        };

    private static EditSample UnpackInstanceSample =>
        new(
            nameof(UnpackInstance),
            static _ => Unpacking([SceneBuilder.NodeData(UnpackedBody, "Body")]),
            [new SceneChange.ComponentRemoved(SampleScene.Car, SampleScene.CarInstance, SceneSchema.SceneInstance.Kind), new SceneChange.NodesAdded([UnpackedBody]), new SceneChange.LayoutChanged()],
            [new SceneChange.NodesRemoved([UnpackedBody]), new SceneChange.LayoutChanged(), new SceneChange.ComponentAdded(SampleScene.Car, SampleScene.CarInstance)])
        {
            HasTargets = true,
            Tamper = static _ =>
            {
                var body = SceneBuilder.NodeData(UnpackedBody, "Body");
                var contents = new List<SceneNodeData> { body };
                return new TamperedEdit(
                    Unpacking(contents),
                    () =>
                    {
                        body.Components.Clear();
                        contents.Clear();
                    });
            },
        };

    // A11 stands for the copy of a node of an instance A1 was unpacked from.
    private static EditSample RestoreInstanceSample =>
        new(
            nameof(RestoreInstance),
            static _ => new RestoreInstance(
                SampleScene.A1,
                InstanceUnpack.One,
                new SceneInstanceComponentData { Id = RestoredInstance, Name = "Scene Instance", Source = SampleScene.CarModel },
                Index: 4,
                [new UnpackedNode(CarBody, SampleScene.A11)]),
            [new SceneChange.NodesRemoved([SampleScene.A11]), new SceneChange.LayoutChanged(), new SceneChange.ComponentAdded(SampleScene.A1, RestoredInstance)],
            [new SceneChange.ComponentRemoved(SampleScene.A1, RestoredInstance, SceneSchema.SceneInstance.Kind), new SceneChange.NodesAdded([SampleScene.A11]), new SceneChange.LayoutChanged()])
        {
            HasTargets = true,
        };

    // Tower's move of Post goes into Tower.oscene, whose file the fixture's project holds.
    private static EditSample ApplyToSourceSample =>
        new(
            nameof(ApplyToSource),
            static _ => ApplyingTower(SampleScene.TowerSource(), SampleScene.TowerSource(postX: 2f)),
            [new SceneChange.OverridesChanged(SampleScene.Tower, [SceneSchema.Transform.PositionX.Id])],
            [new SceneChange.OverridesChanged(SampleScene.Tower, [SceneSchema.Transform.PositionX.Id])])
        {
            HasTargets = false,
            Tamper = static _ =>
            {
                var previous = SampleScene.TowerSource();
                var applied = SampleScene.TowerSource(postX: 2f);
                return new TamperedEdit(ApplyingTower(previous, applied), () =>
                {
                    Array.Clear(previous);
                    Array.Clear(applied);
                });
            },
        };

    // An apply undone: the file holds what it wrote, Tower.oscene as the project holds it, and gets back other bytes.
    private static EditSample RestoreSourceSample =>
        new(
            nameof(RestoreSource),
            static _ => new RestoreSource(SampleScene.Tower, SampleScene.TowerScene)
            {
                Changes = SampleScene.TowerChanges,
                Restored = InstanceChanges.None,
                Applied = SampleScene.TowerSource(),
                Previous = SampleScene.TowerSource(postX: 5f),
            },
            [new SceneChange.OverridesChanged(SampleScene.Tower, [SceneSchema.Transform.PositionX.Id])],
            [new SceneChange.OverridesChanged(SampleScene.Tower, [SceneSchema.Transform.PositionX.Id])])
        {
            HasTargets = false,
        };

    private static EditSample SetStartingCameraSample =>
        new(
            nameof(SetStartingCamera),
            static _ => new SetStartingCamera(NodeReference.To(SampleScene.Cam)),
            [new SceneChange.StartingCameraChanged()],
            [new SceneChange.StartingCameraChanged()]);

    private static EditSample SetStartingInputContextsSample =>
        new(
            nameof(SetStartingInputContexts),
            static _ => new SetStartingInputContexts([new StartingInputContextData(SampleScene.PlayInput)]),
            [new SceneChange.StartingInputContextsChanged()],
            [new SceneChange.StartingInputContextsChanged()])
        {
            CarriesAssetReference = true,
            Asset = SampleScene.PlayInput,
            Tamper = static _ =>
            {
                var contexts = new List<StartingInputContextData> { new(SampleScene.PlayInput) };
                return new TamperedEdit(new SetStartingInputContexts(contexts), contexts.Clear);
            },
        };

    private static EditSample SetSceneReferencesSample =>
        new(
            nameof(SetSceneReferences),
            static _ => new SetSceneReferences(new SceneReferencesData { Scripts = [SpinScript] }),
            [new SceneChange.SceneReferencesChanged()],
            [new SceneChange.SceneReferencesChanged()])
        {
            CarriesAssetReference = true,
            Asset = SpinScript,
            Tamper = static _ =>
            {
                var scripts = new List<AssetPath> { SpinScript };
                return new TamperedEdit(new SetSceneReferences(new SceneReferencesData { Scripts = scripts }), () => scripts.Add(JumpScript));
            },
        };

    private static EditSample SetScriptSlotsSample =>
        new(
            nameof(SetScriptSlots),
            static _ => new SetScriptSlots(SampleScene.Script, [new ScriptSlot { Id = SampleScene.MoveSlot, Script = JumpScript }]),
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)],
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)])
        {
            HasTargets = true,
            CarriesAssetReference = true,
            Asset = JumpScript,
            AssetNode = SampleScene.Script,
        };

    private static EditSample AddScriptSlotSample =>
        ScriptSlotSample(nameof(AddScriptSlot), static _ => new AddScriptSlot(SampleScene.Script, AddedSlot));

    private static EditSample RemoveScriptSlotSample =>
        ScriptSlotSample(nameof(RemoveScriptSlot), static _ => new RemoveScriptSlot(SampleScene.Script, SampleScene.MoveSlotId));

    private static EditSample MoveScriptSlotSample =>
        ScriptSlotSample(nameof(MoveScriptSlot), static _ => new MoveScriptSlot(SampleScene.Script, SampleScene.MoveSlotId, Offset: 1));

    private static EditSample SetSlotScriptSample =>
        new(
            nameof(SetSlotScript),
            static _ => new SetSlotScript(SampleScene.Script, SampleScene.MoveSlotId, JumpScript),
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)],
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)])
        {
            HasTargets = true,
            CarriesAssetReference = true,
            Asset = JumpScript,
            AssetNode = SampleScene.Script,
        };

    private static EditSample SetSlotExecutionOrderSample =>
        ScriptSlotSample(nameof(SetSlotExecutionOrder), static _ => new SetSlotExecutionOrder(SampleScene.Script, SampleScene.MoveSlotId, Order: 5));

    private static EditSample SetScriptValueSample =>
        new(
            nameof(SetScriptValue),
            static _ => new SetScriptValue(SampleScene.Script, SampleScene.MoveSlotId, SampleScene.SurfaceParameter, ScriptValue.FromAsset(GreenMaterial)),
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)],
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)])
        {
            HasTargets = true,
            CarriesAssetReference = true,
            Asset = GreenMaterial,
            AssetNode = SampleScene.Script,
        };

    private static EditSample ResetScriptValueSample =>
        ScriptSlotSample(nameof(ResetScriptValue), static _ => new ResetScriptValue(SampleScene.Script, SampleScene.MoveSlotId, "target"));

    private static EditSample RemoveStaleScriptValuesSample =>
        new(
            nameof(RemoveStaleScriptValues),
            static _ => new RemoveStaleScriptValues(SampleScene.Script, SampleScene.MoveSlotId, []),
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)],
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)])
        {
            HasTargets = true,
            Tamper = static _ =>
            {
                var declared = new List<ScriptParameter>();
                return new TamperedEdit(new RemoveStaleScriptValues(SampleScene.Script, SampleScene.MoveSlotId, declared), () => declared.Add(SampleScene.TargetParameter));
            },
        };

    private static EditSample SetEditorHiddenSample =>
        new(
            nameof(SetEditorHidden),
            static _ => new SetEditorHidden([NodeReference.To(SampleScene.B)], hidden: true),
            [new SceneChange.EditorVisibilityChanged([NodeReference.To(SampleScene.B)])],
            [new SceneChange.EditorVisibilityChanged([NodeReference.To(SampleScene.B)])]);

    // A batch raises its inner changes in order; its revert raises the inner reverts' in reverse order (3.5).
    private static EditSample BatchSample =>
        new(
            "Batch",
            static _ => new SceneEditBatch("Rename both", [new RenameNode(SampleScene.A, "X"), new RenameNode(SampleScene.B, "Y")]),
            [new SceneChange.NodeRenamed(SampleScene.A), new SceneChange.NodeRenamed(SampleScene.B)],
            [new SceneChange.NodeRenamed(SampleScene.B), new SceneChange.NodeRenamed(SampleScene.A)])
        {
            ChangesInOrder = true,
        };

    private static EditSample NestedBatchSample =>
        new(
            "NestedBatch",
            static _ => new SceneEditBatch(
                "Outer",
                [
                    new RenameNode(SampleScene.A, "X"),
                    new SceneEditBatch("Inner", [new RenameNode(SampleScene.B, "Y"), new SetEditorHidden([NodeReference.To(SampleScene.Cam)], hidden: true)]),
                ]),
            [new SceneChange.NodeRenamed(SampleScene.A), new SceneChange.NodeRenamed(SampleScene.B), new SceneChange.EditorVisibilityChanged([NodeReference.To(SampleScene.Cam)])],
            [new SceneChange.EditorVisibilityChanged([NodeReference.To(SampleScene.Cam)]), new SceneChange.NodeRenamed(SampleScene.B), new SceneChange.NodeRenamed(SampleScene.A)])
        {
            ChangesInOrder = true,
        };

    /// <summary>
    /// Gets the parent of a sample scene node.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <returns>Its parent, or <see langword="null"/> for a root node.</returns>
    public static NodeId? ParentOf(NodeId node)
        => node == SampleScene.A1 || node == SampleScene.A2 ? SampleScene.A
        : node == SampleScene.A11 ? SampleScene.A1
        : null;

    /// <summary>
    /// Gets where a moved asset goes: the same location with <c>-moved</c> before the extension.
    /// </summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The new location.</returns>
    public static AssetPath MovedTo(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var text = asset.VirtualPath;
        var dot = text.LastIndexOf('.');
        return AssetPath.Parse(text[..dot] + "-moved" + text[dot..]);
    }

    private static NodeId? ParentOfFirstTarget(EditSample sample) => ParentOf(sample.Build(SampleScene.Build()).Targets.First());

    // A gesture edit of the sample node's script slots: it targets the node and raises one slots change both ways.
    private static EditSample ScriptSlotSample(string name, Func<SceneData, ISceneEdit> build)
        => new(
            name,
            build,
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)],
            [new SceneChange.ScriptSlotsChanged(SampleScene.Script)])
        {
            HasTargets = true,
        };

    private static IEnumerable<TestDataRow<EditSample>> Rows(IEnumerable<EditSample> samples)
        => samples.Select(static sample => new TestDataRow<EditSample>(sample) { DisplayName = sample.Name });

    private static SceneNodeData InsertedNodeData()
        => SceneBuilder.NodeData(
            InsertedNode,
            "Inserted",
            components: [new GeometryComponentData { Id = InsertedGeometry, Name = "Geometry", GeometryUri = SampleScene.SphereGeometry }]);

    private static ApplyToSource ApplyingTower(byte[] previous, byte[] applied)
        => new(SampleScene.Tower, SampleScene.TowerScene)
        {
            Changes = SampleScene.TowerChanges,
            Remaining = InstanceChanges.None,
            Previous = previous,
            Applied = applied,
        };

    private static UnpackInstance Unpacking(IReadOnlyList<SceneNodeData> contents)
        => new(SampleScene.Car, InstanceUnpack.One)
        {
            Source = SampleScene.CarModel,
            Changes = InstanceChanges.None,
            Contents = contents,
            Copies = [new UnpackedNode(CarBody, UnpackedBody)],
        };

    private static NodeInsert InsertAtA2(SceneNodeData node)
        => new(node, new NodePosition(SampleScene.A2, Index: 0), LayoutPosition.UnderNode(SampleScene.A2, 0));

    private static ExplorerEntryData RestoredFolderEntry(string name)
        => new() { Type = "Folder", FolderId = RestoredFolder, Name = name, IsExpanded = true, Children = [] };

    private static List<LayoutEntryId> RestoredChildren() => [LayoutEntryId.Of(SampleScene.Cam), LayoutEntryId.Of(SampleScene.Ortho)];

    private static RestoreFolder RestoringFolder(IReadOnlyList<LayoutEntryId> children, ExplorerEntryData entry)
        => new(entry, LayoutPosition.AtRoot(2), children);

    private static SetOverrides SurfaceMaterial(AssetPath? material)
        => new(
            [SampleScene.Mesh],
            [new OverrideWrite(SceneSchema.Geometry.Surface.Material.Id, CubeSurfaceTarget.Part, CubeSurfaceTarget.LayoutRevision, material) { PartOwner = CubeSurfaceTarget.Geometry }],
            "Edit Surface Material");

    private static PerspectiveCameraData CameraToAdd()
        => new() { Id = AddedCamera, Name = "Camera", NearPlane = 0.1f, FarPlane = 100f, AspectMode = CameraAspectMode.Auto };

    private static SetProperties MovingA1()
        => new(
            new Dictionary<NodeId, PropertyEdit> { [SampleScene.A1] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionX, 5f) },
            "Move");
}
