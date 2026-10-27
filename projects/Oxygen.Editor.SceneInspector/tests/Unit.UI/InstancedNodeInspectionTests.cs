// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// A node an instance brings in shows in the Properties pane (scene instances 6.2, 8.5): a transient node holding the
/// effective values the engine reads, its header naming its source and refusing structural commands; its sections edit
/// it through its instance root's changes, each change marked by the override bar and undone by its reset.
/// </summary>
[TestClass]
internal sealed class InstancedNodeInspectionTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private const string Revision = "0101010101010101010101010101010101010101010101010101010101010101";

    private static readonly AssetPath Car = AssetPath.Parse("/Content/Models/Car.glb");
    private static readonly AssetPath DoorGeometry = AssetPath.Parse("/Content/Models/Car.glb/Door.ogeo");
    private static readonly AssetPath Paint = AssetPath.Parse("/Content/Models/Car.glb/Paint.omat");
    private static readonly NodeId Door = NodeId.New();
    private static readonly Guid Rim = SceneAuthoringFixture.TargetFor(DoorGeometry).SurfaceId;

    public TestContext TestContext { get; set; } = null!;

    /// <summary>
    /// The selected instanced node shows its source's name and its values in its components; its sections take input, and
    /// no lock is reported.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InstancedNodeShowsItsValues() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform");

        SelectDoor(fixture);

        var node = host.SelectedNode!;
        _ = node.Metadata.Name.Should().Be("Door_FL");
        _ = node.Transform.LocalPosition.X.Should().Be(2f);
        _ = node.Flags.Visibility.Should().Be(VisibilitySource.Hidden);
        _ = node.Components.OfType<PointLightComponent>().Should().ContainSingle().Which.Range.Should().Be(7f);
        var geometry = node.Components.OfType<GeometryComponent>().Should().ContainSingle().Subject;
        _ = geometry.Geometry.Should().Be(DoorGeometry);
        _ = geometry.Overrides.Find(SceneSchema.Geometry.Surface.Material.Id, new PartId(SceneSchema.Geometry.Surface.Material.Id.PartKind!, Rim))!.Value.Value.Should().Be(Paint);
        _ = host.CanEditSelectedNodes.Should().BeTrue();
        _ = host.HasLockedSelection.Should().BeFalse();
        _ = host.PropertyEditors.Should().NotBeEmpty().And.OnlyContain(static section => section.IsInputEnabled);
        _ = host.CanUseNodeHeader.Should().BeTrue("the header still selects the instance");
        return Task.CompletedTask;
    });

    /// <summary>The sections of an instanced node take input, as those of a node of the scene do.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InstancedNodeSectionsTakeInput() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneInspectorView { ViewModel = host, Width = 360, Height = 500 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var sections = (StackPanel)view.FindName("PropertySections");

        SelectDoor(fixture);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = sections.Children.OfType<Control>().Should().NotBeEmpty().And.OnlyContain(static section => section.IsEnabled);

        host.SelectInstanceCommand.Execute(parameter: null);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = sections.Children.OfType<Control>().Should().NotBeEmpty().And.OnlyContain(static section => section.IsEnabled);
    });

    /// <summary>The header names the node's instance and source, and the structural commands' refusal.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task HeaderNamesTheSourceAndTheRefusal() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform");

        SelectDoor(fixture);

        _ = host.SelectedNodeOrigin.Should().Be("in Car3 · Car.glb (model)");
        _ = host.SelectedNodeStructureRefusal.Should().Be("'Door_FL' comes from Car.glb. Unpack Car3 to change its structure.");
        var details = new SceneNodeDetailsViewModel
        {
            Node = host.SelectedNode,
            Origin = host.SelectedNodeOrigin,
            StructureRefusal = host.SelectedNodeStructureRefusal,
        };
        _ = details.IsInstanced.Should().BeTrue();
        _ = details.CanAddComponents.Should().BeFalse();
        _ = details.AddGeometryCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = details.AddScriptingCommand.CanExecute(parameter: null).Should().BeFalse();
        _ = details.DeleteComponentCommand.CanExecute(host.SelectedNode!.Components.OfType<PointLightComponent>().Single()).Should().BeFalse();
        _ = details.AddComponentToolTip.Should().Be(host.SelectedNodeStructureRefusal);
        _ = details.RemoveComponentToolTip.Should().Be(host.SelectedNodeStructureRefusal);
        return Task.CompletedTask;
    });

    /// <summary>
    /// The header names the source alone when the instance bears its name, as the model's own document places it
    /// (model editor 5.4).
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task HeaderNamesTheSourceAloneWhenTheInstanceBearsItsName() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        var root = fixture.InstanceRoot!;
        using var host = fixture.CreateInspectorHost("Transform");
        root.Metadata.Name = Car.Name;

        fixture.Selection.SetSelection(fixture.TestScene.DocumentId, [new NodeReference(root.Id, NodePath.Of(Door))], "Test");

        _ = host.SelectedNodeOrigin.Should().Be("in Car.glb (model)");
        return Task.CompletedTask;
    });

    /// <summary><i>Select instance</i> selects the instance root.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task SelectInstanceSelectsTheInstanceRoot() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform");
        SelectDoor(fixture);

        _ = host.SelectInstanceCommand.CanExecute(parameter: null).Should().BeTrue();
        host.SelectInstanceCommand.Execute(parameter: null);

        _ = fixture.Selection.GetContext(fixture.TestScene.DocumentId).SelectedNodes.Should().Equal(NodeReference.To(fixture.InstanceRoot!.Id));
        _ = host.SelectedNode.Should().BeSameAs(fixture.InstanceRoot);
        _ = host.SelectedNodeOrigin.Should().BeNull("the instance root is a node of the scene");
        _ = host.CanEditSelectedNodes.Should().BeTrue();
        return Task.CompletedTask;
    });

    /// <summary>An instanced node whose source is not in the mounted content shows the unavailable notice.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task UnmountedSourceShowsTheNotice() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        using var host = fixture.CreateInspectorHost("Transform");

        SelectDoor(fixture);

        _ = host.HasItems.Should().BeFalse();
        _ = host.HasSelectionNotice.Should().BeTrue();
        _ = host.SelectionSummaryText.Should().Be("The selected objects are not available in the current scene.");
        return Task.CompletedTask;
    });

    /// <summary>Content mounted again, as after a reimport, shows the node's new values.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task RemountShowsTheNewValues() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform");
        SelectDoor(fixture);

        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 5f));
        fixture.Structures.NotifyChanged();

        _ = host.SelectedNode!.Transform.LocalPosition.X.Should().Be(5f);
        return Task.CompletedTask;
    });

    /// <summary>
    /// A value edited in a section becomes a change of the instance root, never of the source: the section shows it with
    /// a full override bar, and undo and redo take it out and back.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task EditingAnInstancedValueChangesTheInstanceRoot() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform");
        SelectDoor(fixture);
        var light = host.PropertyEditors.OfType<PointLightViewModel>().Single();
        _ = light.RangeOverride.State.Should().Be(OverrideState.None);

        light.Range.Value = 9f;
        await light.PendingEdits.ConfigureAwait(true);

        _ = ChangeOf(fixture, SceneSchema.PointLight.Range.Id).Should().Be(9f);
        _ = host.SelectedNode!.Components.OfType<PointLightComponent>().Single().Range.Should().Be(9f);
        _ = light.RangeOverride.State.Should().Be(OverrideState.Full);
        _ = light.RangeOverride.ResetCommand.CanExecute(parameter: null).Should().BeTrue();

        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(true);
        _ = ChangeOf(fixture, SceneSchema.PointLight.Range.Id).Should().BeNull();
        _ = host.SelectedNode!.Components.OfType<PointLightComponent>().Single().Range.Should().Be(7f);
        _ = light.RangeOverride.State.Should().Be(OverrideState.None);

        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(true);
        _ = ChangeOf(fixture, SceneSchema.PointLight.Range.Id).Should().Be(9f);
        _ = light.RangeOverride.State.Should().Be(OverrideState.Full);
    });

    /// <summary>A child flag, which a model node cooks as Inherit, is changed and reset like any value.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task ResetTakesTheSourcesFlagBack() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform");
        SelectDoor(fixture);
        var rendering = host.PropertyEditors.OfType<NodeRenderingViewModel>().Single();
        var undoDepth = fixture.Document.History.UndoStack.Count;

        rendering.IsStatic = true;
        await rendering.PendingEdits.ConfigureAwait(true);
        _ = ChangeOf(fixture, SceneSchema.Node.IsStatic.Id).Should().Be(true);
        _ = rendering.IsStaticOverride.State.Should().Be(OverrideState.Full);

        rendering.IsStaticOverride.ResetCommand.Execute(parameter: null);
        await rendering.PendingEdits.ConfigureAwait(true);

        _ = ChangeOf(fixture, SceneSchema.Node.IsStatic.Id).Should().BeNull();
        _ = host.SelectedNode!.Flags.IsStatic.Should().BeFalse();
        _ = rendering.IsStaticOverride.State.Should().Be(OverrideState.None);
        _ = fixture.Document.History.UndoStack.Count.Should().Be(undoDepth + 2, "the edit and its reset are one entry each");
    });

    /// <summary>
    /// Geometry rows mark only the scene's changes: a surface value the source sets shows without a bar, a policy the
    /// scene sets shows with one, and the geometry asset, which the source decides, cannot be replaced.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task GeometryMarksOnlyTheScenesChanges() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.PublishSurface(DoorGeometry);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform");
        SelectDoor(fixture);
        var geometry = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await geometry.RefreshSurfacesAsync().ConfigureAwait(true);

        _ = geometry.CanChangeGeometry.Should().BeFalse();
        var card = geometry.Surfaces.Should().ContainSingle().Subject;
        _ = card.MaterialOverrideState.Should().Be(OverrideState.None, "the source's material is no change of this scene");

        await geometry.SetLodPolicyAsync(new LodPolicy.Fixed(0), "Edit Level of Detail").ConfigureAwait(true);

        _ = ChangeOf(fixture, SceneSchema.Geometry.LodPolicy.Id).Should().Be(new LodPolicy.Fixed(0));
        _ = geometry.LevelOfDetail.OverrideState.Should().Be(OverrideState.Full);
    });

    private static object? ChangeOf(SceneAuthoringFixture fixture, PropertyId property)
        => fixture.InstanceRoot!.Components.OfType<SceneInstanceComponent>().Single().Changes
            .Of(NodePath.Of(Door)).Of(property.ComponentKind).Find(property, part: null)?.Value;

    private static void SelectDoor(SceneAuthoringFixture fixture)
        => fixture.Selection.SetSelection(fixture.TestScene.DocumentId, [new NodeReference(fixture.InstanceRoot!.Id, NodePath.Of(Door))], "Test");

    private static InstancedNodeValues DoorValues(float positionX)
        => new(
            "Door_FL",
            DoorGeometry,
            [
                new(SceneSchema.Transform.PositionX.Id, Guid.Empty, LayoutRevision: null, positionX),
                new(SceneSchema.Transform.Rotation.Id, Guid.Empty, LayoutRevision: null, new Vector3(0f, 90f, 0f)),
                new(SceneSchema.Node.Visibility.Id, Guid.Empty, LayoutRevision: null, VisibilitySource.Hidden),
                new(SceneSchema.PointLight.Range.Id, Guid.Empty, LayoutRevision: null, 7f),
                new(SceneSchema.Geometry.Surface.Material.Id, Rim, Revision, Paint),
            ]);
}
