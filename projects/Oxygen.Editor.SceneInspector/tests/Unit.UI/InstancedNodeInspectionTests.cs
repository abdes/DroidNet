// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// A node an instance brings in shows in the Properties pane (scene instances 6.2, 8.5): a transient node holding the
/// effective values the engine reads, read-only, its header naming its source and refusing structural commands.
/// </summary>
[TestClass]
internal sealed class InstancedNodeInspectionTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private const string Revision = "0101010101010101010101010101010101010101010101010101010101010101";

    private static readonly AssetPath Car = AssetPath.Parse("/Content/Models/Car.glb");
    private static readonly AssetPath DoorGeometry = AssetPath.Parse("/Content/Models/Car.glb/Door.ogeo");
    private static readonly AssetPath Paint = AssetPath.Parse("/Content/Models/Car.glb/Paint.omat");
    private static readonly NodeId Door = NodeId.New();
    private static readonly Guid Rim = Guid.NewGuid();

    public TestContext TestContext { get; set; } = null!;

    /// <summary>
    /// The selected instanced node shows its source's name and its values in its components; nothing is editable, and no
    /// lock is reported.
    /// </summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InstancedNodeShowsItsValuesReadOnly() => EnqueueAsync(() =>
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
        _ = host.CanEditSelectedNodes.Should().BeFalse();
        _ = host.HasLockedSelection.Should().BeFalse("an instanced node is read-only without being locked");
        _ = host.PropertyEditors.Should().NotBeEmpty().And.OnlyContain(static section => !section.IsInputEnabled);
        _ = host.CanUseNodeHeader.Should().BeTrue("the header still selects the instance");
        return Task.CompletedTask;
    });

    /// <summary>The sections of an instanced node show their fields disabled; those of a node of the scene take input.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task InstancedNodeSectionsAreDisabled() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture(instanceSource: Car);
        fixture.Structures.PublishValues(Car, NodePath.Of(Door), DoorValues(positionX: 2f));
        using var host = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneInspectorView { ViewModel = host, Width = 360, Height = 500 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var sections = (StackPanel)view.FindName("PropertySections");

        SelectDoor(fixture);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = sections.Children.OfType<Control>().Should().NotBeEmpty().And.OnlyContain(static section => !section.IsEnabled);

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
        fixture.Structures.RaiseChanged();

        _ = host.SelectedNode!.Transform.LocalPosition.X.Should().Be(5f);
        _ = host.CanEditSelectedNodes.Should().BeFalse();
        return Task.CompletedTask;
    });

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
