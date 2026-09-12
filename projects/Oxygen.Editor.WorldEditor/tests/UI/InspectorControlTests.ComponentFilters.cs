// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises type filters through the real compact inspector controls.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>All, individual types and repeated toggles change visible sections without authored edits.</summary>
    /// <returns>The asynchronous inspector regression.</returns>
    [TestMethod]
    public Task ComponentFilterControlsToggleSectionsWithoutChangingAuthoring() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        MakeGeometryOnly(fixture);
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneNodeEditorView { ViewModel = model, Width = 360, Height = 600 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var original = model.PropertyEditors.ToArray();
        var version = fixture.Context.Metadata.ChangeVersion;
        _ = original.Should().HaveCount(2);
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
        using var fixture = new Fixture();
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        var second = new SceneNode(fixture.Scene) { Name = "Second" };
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
        _ = camera.FieldOfViewIsIndeterminate.Should().BeTrue();
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
        using var fixture = new Fixture();
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
        using var fixture = new Fixture();
        SetNumericSource(fixture.Node, "Camera", 60);
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        var camera = (PerspectiveCameraViewModel)model.PropertyEditors.Single();
        var view = new SceneNodeEditorView { ViewModel = model, Width = 360, Height = 650 };
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

    /// <summary>Components without property editors remain removable and explain the empty section.</summary>
    /// <returns>The asynchronous component action regression.</returns>
    [TestMethod]
    public Task ComponentWithoutEditorRemainsSelectableForRemoval() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        MakeGeometryOnly(fixture);
        var point = new PointLightComponent { Name = "Point Light" };
        _ = fixture.Node.AddComponent(point);
        using var model = fixture.CreateInspectorHost("Transform", realizeViews: true);
        var view = new SceneNodeEditorView { ViewModel = model, Width = 360, Height = 500 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        Toggle(ComponentButton(view, typeof(PointLightComponent)));
        _ = model.PropertyEditors.Should().BeEmpty();
        _ = model.UnavailableComponentEditorMessage.Should().Contain("Point Light");
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
        using var fixture = new Fixture();
        using var model = fixture.CreateInspectorHost("Camera");
        model.SelectComponentFilter(typeof(PerspectiveCamera));
        var scene = new Scene(Mock.Of<IProject>()) { Name = "Other scene" };
        var node = new SceneNode(scene) { Name = "Other node" };
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
        _ = model.PropertyEditors.Should().HaveCount(2);
        _ = model.ComponentFilters.Should().NotContain(option => option.ComponentType == typeof(PerspectiveCamera));
        _ = metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    /// <summary>A filter change opens its section at the top instead of retaining an unrelated scroll offset.</summary>
    /// <returns>The asynchronous scroll regression.</returns>
    [TestMethod]
    public Task ComponentFilterChangeRevealsSectionHeader() => EnqueueAsync(async () =>
    {
        using var fixture = new Fixture();
        using var model = fixture.CreateInspectorHost("Camera", realizeViews: true);
        var view = new SceneNodeEditorView { ViewModel = model, Width = 360, Height = 350 };
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
        using var fixture = new Fixture();
        MakeGeometryOnly(fixture);
        using var model = fixture.CreateInspectorHost("Transform");
        var component = fixture.Node.Components.OfType<GeometryComponent>().Single();
        var header = new SceneNodeDetailsView { HistoryRoot = model, IsAllComponentsSelected = false };
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
        var other = new SceneNode(fixture.Scene) { Name = "Other node" };
        fixture.Scene.RootNodes.Add(other);
        header.Node = other;
        _ = header.ViewModel.SelectedComponent.Should().BeNull();
        _ = header.DeleteSelectedComponent().Should().BeFalse();
        _ = fixture.Node.Components.Should().Contain(component);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });

    private static void MakeGeometryOnly(Fixture fixture)
    {
        foreach (var component in fixture.Node.Components.Where(component => component is not TransformComponent).ToArray())
        {
            _ = fixture.Node.RemoveComponent(component);
        }

        _ = fixture.Node.AddComponent(CreateInspectorGeometry("Custom instance name"));
    }

    private static GeometryComponent CreateInspectorGeometry(string name = "Geometry") => new()
    {
        Name = name,
        Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
    };

    private static ToggleButton ComponentButton(SceneNodeEditorView view, Type type)
        => view.FindDescendant<ToggleButton>(button => button.Tag is InspectorComponentFilter option && option.ComponentType == type)!;

    private static void Toggle(ToggleButton button)
        => ((IToggleProvider)new ToggleButtonAutomationPeer(button).GetPattern(PatternInterface.Toggle)).Toggle();
}
