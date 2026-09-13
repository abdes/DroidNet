// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises preview demand through active documents, real assignments, and undo history.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Opening a scene requests its saved dependencies; metadata and duplicate load messages create no additional cooks.</summary>
    /// <returns>The asynchronous scene-demand regression.</returns>
    [TestMethod]
    public Task SceneActivationDemandsOnlyReferencedAssets() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Geometry.OverrideSlots.Add(new MaterialsSlot { Material = new(fixture.MaterialUri) });
        fixture.Activate();
        _ = fixture.Requests.Select(static request => request.uri).Should().BeEquivalentTo([fixture.GeometryUri, fixture.MaterialUri]);
        fixture.Activate();
        fixture.NotifyEdited();
        _ = fixture.Requests.Should().HaveCount(2);
        fixture.SwitchDocument(Guid.NewGuid());
        _ = fixture.Requests.Should().OnlyContain(request => request.token.IsCancellationRequested);
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>The rendered geometry picker accepts uncooked content immediately, and Undo retires demand without creating a Redo cook.</summary>
    /// <returns>The asynchronous picker and history regression.</returns>
    [TestMethod]
    public Task UncookedGeometryPickerAssignmentOwnsDemandThroughUndo() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", realizeViews: true, contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var view = new GeometryView { ViewModel = model, Width = 440 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var choice = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal)).Items.Single().Item;
        _ = choice.IsEnabled.Should().BeTrue("a valid saved uncooked geometry must be assignable");
        _ = choice.DisplayType.Should().Contain("Needs cooking");
        var button = (SplitButton)view.FindName("AssetSplitButton");
        await PickAssetAsync(button, "Custom", material: false, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Geometry.Geometry!.Uri.Should().Be(fixture.GeometryUri);
        _ = fixture.Authoring.Context.Metadata.IsDirty.Should().BeTrue("the accepted assignment must dirty the consuming scene");
        _ = fixture.Authoring.Context.History.UndoStack.Should().ContainSingle();
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.GeometryUri);
        await fixture.Authoring.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Geometry.Geometry!.Uri.Should().Be(AssetUris.BuildGeneratedUri("BasicShapes/Cube"));
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue("Undo must detach the original geometry demand");
        await fixture.Authoring.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Geometry.Geometry!.Uri.Should().Be(fixture.GeometryUri);
        _ = model.SelectedAssetName.Should().Be("Custom");
        _ = fixture.Requests.Should().ContainSingle();
    });

    /// <summary>Material assignment requests its source, and changing selection detaches that demand.</summary>
    /// <returns>The asynchronous material selection regression.</returns>
    [TestMethod]
    public Task MaterialAssignmentDemandEndsWithSelection() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.ApplyMaterialAsync(new("Material", fixture.MaterialUri, "Material", "/Content/Material.omat.json", AssetPickerGroup.Content, IsEnabled: true, ThumbnailModel: "\uE790")).ConfigureAwait(true);
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(fixture.MaterialUri);
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.MaterialUri);
        _ = fixture.Authoring.Messenger.Send(new SceneNodeSelectionChangedMessage([]));
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
    });

    /// <summary>Undo of a material assignment detaches its cook while Redo changes only authoring intent.</summary>
    /// <returns>The asynchronous material-history regression.</returns>
    [TestMethod]
    public Task MaterialDemandRetiresOnUndoWithoutCookingRedo() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.ApplyMaterialAsync(new("Material", fixture.MaterialUri, "Material", "/Content/Material.omat.json", AssetPickerGroup.Content, IsEnabled: true, ThumbnailModel: "\uE790")).ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
        await fixture.Authoring.Context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await fixture.Authoring.Context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
        _ = fixture.Geometry.OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(fixture.MaterialUri);
    });

    /// <summary>Removing the last scene use retires activation demand without relying on a dirty transition.</summary>
    /// <returns>The asynchronous structural-reference regression.</returns>
    [TestMethod]
    public Task SceneDemandRetiresWhenItsReferencedNodeIsRemoved() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        _ = fixture.Requests.Should().ContainSingle();
        _ = fixture.Authoring.Scene.RootNodes.Remove(fixture.Authoring.Node);
        _ = fixture.Requests.Single().token.IsCancellationRequested.Should().BeTrue();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle();
    });

    /// <summary>A late identity lookup cannot submit work after its scene closes.</summary>
    /// <returns>The asynchronous lookup-lifetime regression.</returns>
    [TestMethod]
    public Task ClosedSceneRejectsLateDemandLookup() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        var resolved = new TaskCompletionSource<ContentBrowserAssetItem?>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Assets.Setup(value => value.ResolveAsync(fixture.GeometryUri, It.IsAny<CancellationToken>())).Returns(resolved.Task);
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        fixture.Activate();
        fixture.Authoring.Documents.Raise(value => value.DocumentClosed += null, new DocumentClosedEventArgs(default, fixture.Authoring.Context.Metadata));
        resolved.SetResult(fixture.GeometryAsset);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().BeEmpty();
    });

    /// <summary>Cooked/current identities and engine-owned assets do not launch demand cooks.</summary>
    /// <returns>The asynchronous no-work regression.</returns>
    [TestMethod]
    public Task CurrentAndBuiltinSceneReferencesNeedNoDemandCook() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        _ = fixture.Assets.Setup(value => value.ResolveAsync(fixture.GeometryUri, It.IsAny<CancellationToken>()))
            .ReturnsAsync(fixture.GeometryAsset with { CookStatus = fixture.GeometryAsset.CookStatus! with { Freshness = AssetCookFreshness.Current, HasVerifiedOutput = true } });
        fixture.Geometry.OverrideSlots.Add(new MaterialsSlot { Material = new(AssetUris.BuildGeneratedUri("Materials/Default")) });
        fixture.Activate();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Requests.Should().BeEmpty();
        fixture.Assets.Verify(value => value.ResolveAsync(AssetUris.BuildGeneratedUri("Materials/Default"), It.IsAny<CancellationToken>()), Times.Never);
    });

    /// <summary>A delayed accepted edit cannot create demand or overwrite the display for a newer selection.</summary>
    /// <returns>The asynchronous assignment-completion regression.</returns>
    [TestMethod]
    public Task ObsoleteGeometryAssignmentCompletionDoesNotDemandOrReplaceSelection() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        var completed = new TaskCompletionSource<SceneCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var commands = new Mock<ISceneDocumentCommandService>();
        _ = commands.Setup(value => value.EditPropertiesAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<PropertyEdit>(), It.IsAny<string>(), It.IsAny<EditSessionToken>())).Returns(completed.Task);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", commandService: commands.Object, contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var choice = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal)).Items.Single().Item;
        var edit = model.ApplyAssetAsync(choice);
        var other = new SceneNode(fixture.Authoring.Scene) { Name = "Other" };
        _ = other.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new(AssetUris.BuildGeneratedUri("BasicShapes/Sphere")) });
        fixture.Authoring.Scene.RootNodes.Add(other);
        _ = fixture.Authoring.Messenger.Send(new SceneNodeSelectionChangedMessage([other]));
        await WaitForRenderAsync().ConfigureAwait(true);
        var expected = model.SelectedAssetUriString;
        completed.SetResult(SceneCommandResult.Success);
        await edit.ConfigureAwait(true);
        _ = model.SelectedAssetUriString.Should().Be(expected).And.Contain("Sphere");
        _ = fixture.Requests.Should().BeEmpty();
    });

    /// <summary>Hiding the component editor does not discard preview demand for an accepted edit on the same nodes.</summary>
    /// <returns>The asynchronous component-filter regression.</returns>
    [TestMethod]
    public Task AcceptedGeometryAssignmentStillDemandsWhenItsSectionIsHidden() => EnqueueAsync(async () =>
    {
        using var fixture = new DemandFixture();
        fixture.Activate();
        var completed = new TaskCompletionSource<SceneCommandResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var commands = new Mock<ISceneDocumentCommandService>();
        _ = commands.Setup(value => value.EditPropertiesAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<PropertyEdit>(), It.IsAny<string>(), It.IsAny<EditSessionToken>())).Returns(completed.Task);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", commandService: commands.Object, contentDemand: fixture.Service, assetProvider: fixture.Assets.Object);
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        var choice = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal)).Items.Single().Item;
        var edit = model.ApplyAssetAsync(choice);
        fixture.Geometry.Geometry = new(fixture.GeometryUri);
        model.SetInputEnabled(enabled: false);
        completed.SetResult(SceneCommandResult.Success);
        await edit.ConfigureAwait(true);
        _ = fixture.Requests.Should().ContainSingle().Which.uri.Should().Be(fixture.GeometryUri);
    });

    private sealed partial class DemandFixture : IDisposable
    {
        private readonly ProjectContextService projects = new();
        private readonly Mock<IContentPipelineService> pipeline = new();
        private Guid activeDocument;

        public DemandFixture()
        {
            var info = new ProjectInfo("Demand", Category.Games, "C:/Demand", "preview.png");
            _ = Mock.Get(this.Authoring.Scene.Project).SetupGet(value => value.ProjectInfo).Returns(info);
            this.projects.Activate(new ProjectContext
            {
                ProjectId = info.Id, Name = info.Name, Category = Category.Games, ProjectRoot = "C:/Demand",
                AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
            });
            this.Geometry = new() { Name = "Geometry", Geometry = new(AssetUris.BuildGeneratedUri("BasicShapes/Cube")) };
            _ = this.Authoring.Node.AddComponent(this.Geometry);
            this.activeDocument = this.Authoring.Scene.Id;
            _ = this.Authoring.Documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(() => this.activeDocument);
            _ = this.Authoring.Documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([this.Authoring.Context.Metadata]);
            _ = this.Authoring.Documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>()))
                .Callback(this.NotifyEdited).ReturnsAsync(value: true);
            var accepted = new SyncOutcome(SyncStatus.Accepted, "Demand assignment", AffectedScope.Empty);
            _ = this.Authoring.Sync.Setup(value => value.AttachGeometryAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
            _ = this.Authoring.Sync.Setup(value => value.UpdateMaterialSlotAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<int>(), It.IsAny<Uri?>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
            this.GeometryAsset = CreateStatusAsset(0) with
            {
                IdentityUri = this.GeometryUri, DisplayName = "Custom", Kind = AssetKind.Geometry, DerivedState = null,
                CookStatus = new(this.GeometryUri, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, HasVerifiedOutput: false, [], [], []),
            };
            var material = CreateStatusAsset(1) with { IdentityUri = this.MaterialUri, CookStatus = new(this.MaterialUri, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, HasVerifiedOutput: false, [], [], []) };
            _ = this.Assets.SetupGet(value => value.Items).Returns(Observable.Return<IReadOnlyList<ContentBrowserAssetItem>>([this.GeometryAsset, material]));
            _ = this.Assets.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
            _ = this.Assets.Setup(value => value.ResolveAsync(this.GeometryUri, It.IsAny<CancellationToken>())).ReturnsAsync(this.GeometryAsset);
            _ = this.Assets.Setup(value => value.ResolveAsync(this.MaterialUri, It.IsAny<CancellationToken>())).ReturnsAsync(material);
            _ = this.pipeline.Setup(value => value.CookPreviewAssetAsync(It.IsAny<Uri>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
                .Returns((Uri uri, ProjectContext project, CancellationToken token) =>
                {
                    _ = project.Should().BeSameAs(this.projects.ActiveProject);
                    this.Requests.Add((uri, token));
                    return new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously).Task.WaitAsync(token);
                });
            this.Service = new(
                CreateStatusHosting(),
                this.Assets.Object,
                this.pipeline.Object,
                this.projects,
                this.Authoring.Documents.Object,
                this.Authoring.Sync.Object,
                Mock.Of<ISceneExplorerService>(),
                this.Authoring.Messenger,
                default,
                NullLogger<SceneContentDemandService>.Instance);
        }

        public Fixture Authoring { get; } = new();

        public Mock<IContentBrowserAssetProvider> Assets { get; } = new();

        public Uri GeometryUri { get; } = new("asset:///Content/Geometry/Custom.ogeo.json");

        public Uri MaterialUri { get; } = new("asset:///Content/Materials/Custom.omat.json");

        public GeometryComponent Geometry { get; }

        public ContentBrowserAssetItem GeometryAsset { get; }

        public SceneContentDemandService Service { get; }

        public List<(Uri uri, CancellationToken token)> Requests { get; } = [];

        public void Activate() => _ = this.Authoring.Messenger.Send(new SceneAuthoringLoadedMessage(this.Authoring.Scene, this.Authoring.Context.Metadata));

        public void NotifyEdited() => this.Authoring.Documents.Raise(value => value.DocumentMetadataChanged += null, new DocumentMetadataChangedEventArgs(default, this.Authoring.Context.Metadata));

        public void SwitchDocument(Guid documentId)
        {
            this.activeDocument = documentId;
            this.Authoring.Documents.Raise(value => value.DocumentActivated += null, new DocumentActivatedEventArgs(default, documentId));
        }

        public void Dispose()
        {
            this.Service.Dispose();
            this.Authoring.Dispose();
        }
    }
}
