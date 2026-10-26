// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.SceneInspector.Components.Rendering;
using Oxygen.Editor.SceneInspector.Components.Transform;
using Oxygen.Editor.SceneInspector.Presentation;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.SceneInspector.TestSupport.InspectorControls;
using static Oxygen.Editor.SceneInspector.TestSupport.SceneTestData;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

[TestClass]
internal sealed class ComponentFiltersTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Verifies lock changes and component filtering keep Inspector inputs disabled.</summary>
    /// <returns>The asynchronous UI regression.</returns>
    [TestMethod]
    public Task LockChangeAndComponentFilterKeepInspectorInputsDisabled() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture(session => new SceneNodeInteraction(
            session,
            Mock.Of<IEditorSettingsManager>(),
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer()));
        var interaction = fixture.Interaction!;
        await interaction.RestoreAsync(fixture.Scene.Id).ConfigureAwait(true);
        using var model = fixture.CreateInspectorHost("Light", realizeViews: true);
        var view = new SceneInspectorView { ViewModel = model, Width = 420, Height = 600 };
        await LoadTestContentAsync(view).ConfigureAwait(true);

        var editor = model.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        _ = editor.IsInputEnabled.Should().BeTrue();
        interaction.SetLocked(NodeReference.To(fixture.Node.Id), isLocked: true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = editor.IsInputEnabled.Should().BeFalse("the lock notification must disable the active component editor");

        model.SelectComponentFilter(typeof(DirectionalLightComponent));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.HasLockedSelection.Should().BeTrue();
        _ = editor.IsInputEnabled.Should().BeFalse("changing filters must not re-enable a locked component editor");
    });

    /// <summary>
    /// Each inspector's component selector lays out its own items: a layout shared by repeaters keeps a torn-down
    /// repeater's handler, and the next inspector's binding then invokes it (WinUI's default is one StackLayout for every
    /// repeater that sets none).
    /// </summary>
    /// <returns>The asynchronous UI regression.</returns>
    [TestMethod]
    public Task ComponentSelectorsDoNotShareALayout() => EnqueueAsync(() =>
    {
        var first = (ItemsRepeater)new SceneInspectorView().FindName("ComponentSelector");
        var second = (ItemsRepeater)new SceneInspectorView().FindName("ComponentSelector");

        _ = first.Layout.Should().NotBeNull();
        _ = first.Layout.Should().NotBeSameAs(second.Layout);
    });

    /// <summary>All, individual types and repeated toggles change visible sections without authored edits.</summary>
    /// <returns>The asynchronous inspector regression.</returns>
    [TestMethod]
    public Task ComponentFilterControlsToggleSectionsWithoutChangingAuthoring() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        MakeGeometryOnly(fixture);
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneInspectorView
        {
            ViewModel = model,
            Width = 360,
            Height = 600,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var original = model.PropertyEditors.ToArray();
        var version = fixture.Document.History.Position;
        _ = original.Select(static editor => editor.GetType()).Should().Equal(
            [typeof(TransformViewModel), typeof(NodeRenderingViewModel), typeof(GeometryViewModel)],
            "the node Rendering section is listed right after Transform in the unfiltered view");
        var geometry = ComponentButton(view, typeof(GeometryComponent));
        _ = ((InspectorComponentFilter)geometry.Tag).Label.Should().Be("Geometry");
        Toggle(geometry);
        _ = model.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<GeometryViewModel>();
        await WaitForRenderAsync().ConfigureAwait(true);
        var geometryHeader = view.FindDescendant<GeometryView>()!.FindDescendant<Oxygen.Editor.Controls.PropertiesExpander>()!;
        var expectedGlyph = (string)new ComponentToGlyphConverter().Convert(typeof(GeometryComponent), typeof(string), null!, string.Empty);
        _ = geometryHeader.HeaderIcon.Should().BeOfType<FontIcon>().Which.Glyph.Should().Be(expectedGlyph);
        _ = model.SelectedComponent.Should().BeSameAs(fixture.Node.Components.OfType<GeometryComponent>().Single());
        Toggle(ComponentButton(view, typeof(TransformComponent)));
        _ = model.PropertyEditors.Should().ContainSingle().Which.Should().BeSameAs(original.OfType<TransformViewModel>().Single());
        Toggle(ComponentButton(view, typeof(TransformComponent)));
        _ = model.PropertyEditors.Should().Equal(original);
        Toggle(geometry);
        var details = (SceneNodeDetailsView)view.FindName("NodeDetails");
        var all = (ToolBarToggleButton)details.FindName("AllComponentsButton");
        _ = all.IsChecked.Should().BeFalse();
        _ = ToolTipService.GetToolTip(all).Should().Be("Show all component properties");
        _ = all.Icon.Should().BeOfType<SymbolIconSource>().Which.Symbol.Should().Be(Symbol.AllApps);
        Toggle(all);
        _ = all.IsChecked.Should().BeTrue();
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = model.PropertyEditors.Should().Equal(original);
        _ = fixture.Document.History.Position.Should().Be(version);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = fixture.Document.IsDirty.Should().BeFalse();
    });

    /// <summary>
    /// A section filtered out and back in shows the same view: the inspector keeps one view per section for as long as
    /// the section exists, instead of building a new one over it each time it shows.
    /// </summary>
    /// <returns>The asynchronous UI regression.</returns>
    [TestMethod]
    public Task SectionFilteredOutAndBackShowsItsSameView() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        MakeGeometryOnly(fixture);
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneInspectorView { ViewModel = model, Width = 360, Height = 600 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var geometry = model.PropertyEditors.OfType<GeometryViewModel>().Single();
        var shown = view.FindDescendant<GeometryView>()!;

        Toggle(ComponentButton(view, typeof(TransformComponent)));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = view.FindDescendant<GeometryView>().Should().BeNull();
        Toggle(ComponentButton(view, typeof(TransformComponent)));
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = view.FindDescendant<GeometryView>().Should().BeSameAs(shown);
        _ = shown.ViewModel.Should().BeSameAs(geometry);
    });

    /// <summary>Multi-node filters preserve target applicability and explain components missing from some nodes.</summary>
    /// <returns>The asynchronous selection regression.</returns>
    [TestMethod]
    public Task ComponentFiltersRespectMixedSelectionAndResetAtSelectionBoundaries() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        var second = new SceneNode(fixture.Scene, "Second");
        _ = second.AddComponent(new PerspectiveCamera { Name = "Camera", FieldOfView = 80 });
        fixture.Scene.RootNodes.Add(second);
        model.SelectComponentFilter(typeof(DirectionalLightComponent));
        fixture.Select(fixture.Node, second);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        var light = model.ComponentFilters.Single(option => option.ComponentType == typeof(DirectionalLightComponent));
        _ = light.IsAvailable.Should().BeFalse();
        _ = light.StatusLabel.Should().Be("1/2");
        _ = light.UnavailableReason.Should().Contain("every selected node");
        model.SelectComponentFilter(typeof(DirectionalLightComponent));
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        var camera = model.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<PerspectiveCameraViewModel>().Which;
        _ = camera.FieldOfView.IsMixed.Should().BeTrue();
        _ = model.SelectedComponent.Should().BeNull("a multi-node filter must not expose a single-node deletion target");
        fixture.Select();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = model.ComponentFilters.Should().BeEmpty();
        _ = model.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<SceneSettingsViewModel>();
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>Component edits keep valid filters and removal returns to All, including through history replay.</summary>
    /// <returns>The asynchronous component lifecycle regression.</returns>
    [TestMethod]
    public Task ComponentRemovalResetsOnlyUnavailableFilterAndUndoRestoresAvailability() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera");
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        _ = fixture.Node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Cube") });
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.SelectedComponentType.Should().Be<PerspectiveCamera>();
        _ = (await fixture.Authoring.ExecuteAsync(new RemoveComponent(fixture.Node.Id, fixture.Camera.Id)).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = model.ComponentFilters.Should().NotContain(option => option.ComponentType == typeof(PerspectiveCamera));
        await fixture.Document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.ComponentFilters.Should().Contain(option => option.ComponentType == typeof(PerspectiveCamera) && option.IsAvailable);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        await fixture.Document.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.ComponentFilters.Should().NotContain(option => option.ComponentType == typeof(PerspectiveCamera));
    });

    /// <summary>Hiding a section cancels its pointer gesture and rejects callbacks from the removed controls.</summary>
    /// <returns>The asynchronous edit lifetime regression.</returns>
    [TestMethod]
    public Task ComponentFilterChangeCancelsHiddenPointerGesture() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        SetNumericSource(fixture.Node, "Camera", 60);
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        var camera = (PerspectiveCameraViewModel)model.PropertyEditors.Single();
        var view = new SceneInspectorView
        {
            ViewModel = model,
            Width = 360,
            Height = 650,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(control => Equals(control.Tag, "FieldOfView"))!;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        number.NumberValue = 90;
        model.SelectComponentFilter(typeof(TransformComponent));
        await camera.PendingEdits.ConfigureAwait(true);
        number.NumberValue = 100;
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await camera.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.FieldOfView.Should().Be(60);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = fixture.Document.IsDirty.Should().BeFalse();
    });

    /// <summary>A point light filters to its own editor section and remains removable.</summary>
    /// <returns>The asynchronous component action regression.</returns>
    [TestMethod]
    public Task PointLightFilterShowsItsEditorAndRemainsRemovable() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        MakeGeometryOnly(fixture);
        var point = new PointLightComponent
        {
            Name = "Point Light",
        };
        _ = fixture.Node.AddComponent(point);
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneInspectorView
        {
            ViewModel = model,
            Width = 360,
            Height = 500,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        Toggle(ComponentButton(view, typeof(PointLightComponent)));
        _ = model.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<PointLightViewModel>();
        _ = model.HasUnavailableComponentEditor.Should().BeFalse();
        var details = (SceneNodeDetailsView)view.FindName("NodeDetails");
        _ = ((Button)details.FindName("DeleteComponentButton")).IsEnabled.Should().BeTrue();
        _ = details.DeleteSelectedComponent().Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Node.Components.Should().NotContain(point);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>Switching the scene document cannot carry a prior component filter into its selection.</summary>
    /// <returns>The asynchronous document-scope regression.</returns>
    [TestMethod]
    public Task ComponentFiltersResetWhenSceneDocumentChanges() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera");
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        var scene = new Scene
        {
            Name = "Other scene",
        };
        var node = new SceneNode(scene, "Other node");
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Cube") });
        scene.RootNodes.Add(node);
        using var metadata = TestScenes.Open(scene);
        scene = metadata.Scene;
        node = metadata.NodeOf(node);
        fixture.Area.Activate(metadata.Document);
        fixture.Selection.SetSelection(metadata.DocumentId, [NodeReference.To(node.Id)], "Test");
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = model.SelectedNode.Should().BeSameAs(node);
        _ = model.PropertyEditors.Select(static editor => editor.GetType()).Should().Equal(
            typeof(TransformViewModel), typeof(NodeRenderingViewModel), typeof(GeometryViewModel));
        _ = model.ComponentFilters.Should().NotContain(option => option.ComponentType == typeof(PerspectiveCamera));
        _ = metadata.Document.IsDirty.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>A filter change opens its section at the top instead of retaining an unrelated scroll offset.</summary>
    /// <returns>The asynchronous scroll regression.</returns>
    [TestMethod]
    public Task ComponentFilterChangeRevealsSectionHeader() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        var view = new SceneInspectorView
        {
            ViewModel = model,
            Width = 360,
            Height = 350,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var scroll = (ScrollViewer)view.FindName("PropertyScroll");
        _ = scroll.ChangeView(horizontalOffset: null, verticalOffset: 100, zoomFactor: null, disableAnimation: true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = scroll.VerticalOffset.Should().BePositive();
        Toggle(ComponentButton(view, typeof(DirectionalLightComponent)));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = scroll.VerticalOffset.Should().Be(0);
    });

    /// <summary>Binding order preserves the selected component, and a later node change rejects the old target.</summary>
    /// <param name="selectedFirst">Whether selection arrives before the node binding.</param>
    /// <returns>The asynchronous header-binding regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ComponentHeaderBindingOrderPreservesOnlyCurrentActionTarget(bool selectedFirst) => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        MakeGeometryOnly(fixture);
        using var model = fixture.CreateInspectorHost("Transform");
        var component = fixture.Node.Components.OfType<GeometryComponent>().Single();
        var header = new SceneNodeDetailsView
        {
            NodeEdits = model,
            IsAllComponentsSelected = false,
        };
        if (selectedFirst)
        {
            header.SelectedComponent = component;
            header.Node = fixture.Node;
        }
        else
        {
            header.Node = fixture.Node;
            header.SelectedComponent = component;
        }

        await LoadTestContentAsync(header).ConfigureAwait(true);
        var remove = (Button)header.FindName("DeleteComponentButton");
        _ = remove.IsEnabled.Should().BeTrue();
        _ = header.ViewModel!.SelectedComponent.Should().BeSameAs(component);
        var other = new SceneNode(fixture.Scene, "Other node");
        fixture.Scene.RootNodes.Add(other);
        header.Node = other;
        _ = header.ViewModel.SelectedComponent.Should().BeNull();
        _ = header.DeleteSelectedComponent().Should().BeFalse();
        _ = fixture.Node.Components.Should().Contain(component);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>A node with no other components lists Transform then Rendering, and never its name or hierarchy.</summary>
    /// <returns>The asynchronous component list regression.</returns>
    [TestMethod]
    public Task GroupNode_ListsTransformThenRendering_AndNeverMetadataOrHierarchy() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var group = new SceneNode(fixture.Scene, "Group");
        fixture.Scene.RootNodes.Add(group);
        fixture.Select(group);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = model.ComponentFilters.Select(static option => option.ComponentType).Should().Equal(typeof(TransformComponent), typeof(NodeFlagsComponent));
        _ = model.ComponentFilters.Should().NotContain(option => option.ComponentType == typeof(MetadataComponent) || option.ComponentType == typeof(HierarchyComponent));
        _ = model.PropertyEditors.Select(static editor => editor.GetType()).Should().Equal(typeof(TransformViewModel), typeof(NodeRenderingViewModel));
    });

    /// <summary>The Rendering component is locked: the node details offer no removal for it.</summary>
    /// <returns>The asynchronous header regression.</returns>
    [TestMethod]
    public Task RenderingComponent_IsLocked_AndCannotBeRemoved() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Transform");
        var header = new SceneNodeDetailsView
        {
            NodeEdits = model,
            Node = fixture.Node,
            SelectedComponent = fixture.Node.Flags,
            IsAllComponentsSelected = false,
        };
        await LoadTestContentAsync(header).ConfigureAwait(true);

        _ = fixture.Node.Flags.IsLocked.Should().BeTrue();
        _ = ((Button)header.FindName("DeleteComponentButton")).IsEnabled.Should().BeFalse();
        _ = header.DeleteSelectedComponent().Should().BeFalse();
        _ = fixture.Node.Components.Should().Contain(fixture.Node.Flags);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });
}
