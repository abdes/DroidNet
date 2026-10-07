// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorControls;
using static Oxygen.Editor.WorldEditor.TestSupport.SceneTestData;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

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
        var interaction = new WorkspaceInteractionService(
            Mock.Of<IEditorSettingsManager>(),
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer());
        using var fixture = new SceneAuthoringFixture(interaction);
        await interaction.RestoreAsync(
            ProjectContext.FromProjectInfo(new ProjectInfo("Inspector lock tests", Category.Games, "H:/InspectorLockTests")),
            fixture.Scene.Id).ConfigureAwait(true);
        using var model = fixture.CreateInspectorHost("Light", realizeViews: true);
        var view = new SceneNodeEditorView { ViewModel = model, Width = 420, Height = 600 };
        await LoadTestContentAsync(view).ConfigureAwait(true);

        var editor = model.PropertyEditors.OfType<DirectionalLightViewModel>().Single();
        _ = editor.IsInputEnabled.Should().BeTrue();
        interaction.SetLocked(fixture.Node.Id, isLocked: true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = editor.IsInputEnabled.Should().BeFalse("the lock notification must disable the active component editor");

        model.SelectComponentFilter(typeof(DirectionalLightComponent));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.HasLockedSelection.Should().BeTrue();
        _ = editor.IsInputEnabled.Should().BeFalse("changing filters must not re-enable a locked component editor");
    });

    /// <summary>All, individual types and repeated toggles change visible sections without authored edits.</summary>
    /// <returns>The asynchronous inspector regression.</returns>
    [TestMethod]
    public Task ComponentFilterControlsToggleSectionsWithoutChangingAuthoring() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        MakeGeometryOnly(fixture);
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneNodeEditorView
        {
            ViewModel = model,
            Width = 360,
            Height = 600,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var original = model.PropertyEditors.ToArray();
        var version = fixture.Context.Metadata.ChangeVersion;
        _ = original.Select(static editor => editor.GetType()).Should().Equal(
            new[] { typeof(TransformViewModel), typeof(GeometryViewModel), typeof(NodeRenderingViewModel) },
            "the node Rendering section follows every component section in the unfiltered view");
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
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(version);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    });

    /// <summary>Multi-node filters preserve target applicability and explain components missing from some nodes.</summary>
    /// <returns>The asynchronous selection regression.</returns>
    [TestMethod]
    public Task ComponentFiltersRespectMixedSelectionAndResetAtSelectionBoundaries() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        var second = new SceneNode(fixture.Scene)
        {
            Name = "Second",
        };
        _ = second.AddComponent(new PerspectiveCamera { Name = "Camera", FieldOfView = 80 });
        fixture.Scene.RootNodes.Add(second);
        model.SelectComponentFilter(typeof(DirectionalLightComponent));
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([fixture.Node, second]));
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
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([]));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = model.ComponentFilters.Should().BeEmpty();
        _ = model.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<EnvironmentViewModel>();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>Component edits keep valid filters and removal returns to All, including through history replay.</summary>
    /// <returns>The asynchronous component lifecycle regression.</returns>
    [TestMethod]
    public Task ComponentRemovalResetsOnlyUnavailableFilterAndUndoRestoresAvailability() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera");
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        _ = fixture.Node.AddComponent(new GeometryComponent { Name = "Geometry" });
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.SelectedComponentType.Should().Be<PerspectiveCamera>();
        _ = (await fixture.Commands.RemoveComponentAsync(fixture.Context, fixture.Node.Id, fixture.Camera.Id).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = model.ComponentFilters.Should().NotContain(option => option.ComponentType == typeof(PerspectiveCamera));
        await fixture.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.ComponentFilters.Should().Contain(option => option.ComponentType == typeof(PerspectiveCamera) && option.IsAvailable);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        await fixture.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
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
        var view = new SceneNodeEditorView
        {
            ViewModel = model,
            Width = 360,
            Height = 650,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var number = view.FindDescendant<NumberBox>(control => Equals(control.Tag, "FieldOfView"))!;
        RaiseNumberEvent(number, "OnEditSessionStarted", NumberBoxEditInteractionKind.PointerDrag);
        number.NumberValue = 90;
        model.SelectComponentFilter(typeof(TransformComponent));
        await camera.PendingEdits.ConfigureAwait(true);
        number.NumberValue = 100;
        RaiseNumberEvent(number, "OnEditSessionCompleted", NumberBoxEditInteractionKind.PointerDrag, NumberBoxEditCompletionKind.Commit);
        await camera.PendingEdits.ConfigureAwait(true);
        _ = fixture.Camera.FieldOfView.Should().Be(60);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
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
        var view = new SceneNodeEditorView
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
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
    });

    /// <summary>Switching the scene document cannot carry a prior component filter into its selection.</summary>
    /// <returns>The asynchronous document-scope regression.</returns>
    [TestMethod]
    public Task ComponentFiltersResetWhenSceneDocumentChanges() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera");
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        var scene = new Scene(Mock.Of<IProject>())
        {
            Name = "Other scene",
        };
        var node = new SceneNode(scene)
        {
            Name = "Other node",
        };
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry" });
        scene.RootNodes.Add(node);
        var metadata = new SceneDocumentMetadata(scene.Id);
        _ = fixture.Documents.Setup(service => service.GetOpenDocuments(It.IsAny<Microsoft.UI.WindowId>())).Returns([fixture.Context.Metadata, metadata]);
        _ = fixture.Documents.Setup(service => service.GetActiveDocumentId(It.IsAny<Microsoft.UI.WindowId>())).Returns(scene.Id);
        _ = fixture.Sync.Setup(service => service.GetDocumentScene(metadata)).Returns(scene);
        _ = fixture.Messenger.Send(new SceneAuthoringLoadedMessage(scene, metadata));
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([node]));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = model.IsAllComponentsSelected.Should().BeTrue();
        _ = model.SelectedNode.Should().BeSameAs(node);
        _ = model.PropertyEditors.Select(static editor => editor.GetType()).Should().Equal(
            typeof(TransformViewModel), typeof(GeometryViewModel), typeof(NodeRenderingViewModel));
        _ = model.ComponentFilters.Should().NotContain(option => option.ComponentType == typeof(PerspectiveCamera));
        _ = metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>A filter change opens its section at the top instead of retaining an unrelated scroll offset.</summary>
    /// <returns>The asynchronous scroll regression.</returns>
    [TestMethod]
    public Task ComponentFilterChangeRevealsSectionHeader() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        var view = new SceneNodeEditorView
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
            HistoryRoot = model,
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
        var other = new SceneNode(fixture.Scene)
        {
            Name = "Other node",
        };
        fixture.Scene.RootNodes.Add(other);
        header.Node = other;
        _ = header.ViewModel.SelectedComponent.Should().BeNull();
        _ = header.DeleteSelectedComponent().Should().BeFalse();
        _ = fixture.Node.Components.Should().Contain(component);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });
}
