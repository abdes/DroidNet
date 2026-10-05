// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Tests;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed partial class DemandFixture : IDisposable
{
    private readonly ProjectContextService projects = new();
    private readonly Mock<IContentPipelineService> pipeline = new();
    private Guid activeDocument;
    public DemandFixture()
    {
        var info = new ProjectInfo("Demand", Category.Games, "C:/Demand", "preview.png");
        _ = Mock.Get(this.Authoring.Scene.Project).SetupGet(value => value.ProjectInfo).Returns(info);
        this.Authoring.Projects.Activate(ProjectContext.FromProjectInfo(info));
        this.projects.Activate(new ProjectContext { ProjectId = info.Id, Name = info.Name, Category = Category.Games, ProjectRoot = "C:/Demand", AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [], });
        this.Geometry = new()
        {
            Name = "Geometry",
            Geometry = new(AssetUris.BuildGeneratedUri("BasicShapes/Cube"))
        };
        _ = this.Authoring.Node.AddComponent(this.Geometry);
        this.activeDocument = this.Authoring.Scene.Id;
        _ = this.Authoring.Documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(() => this.activeDocument);
        _ = this.Authoring.Documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([this.Authoring.Context.Metadata]);
        _ = this.Authoring.Documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).Callback(this.NotifyEdited).ReturnsAsync(value: true);
        var accepted = new SyncOutcome(SyncStatus.Accepted, "Demand assignment", AffectedScope.Empty);
        _ = this.Authoring.Sync.Setup(value => value.AttachGeometryAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        _ = this.Authoring.Sync.Setup(value => value.UpdateMaterialSlotAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<Oxygen.Editor.World.Slots.MaterialSlotTarget>(), It.IsAny<Uri?>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        this.GeometryAsset = CreateStatusAsset(0) with
        {
            IdentityUri = this.GeometryUri,
            DisplayName = "Custom",
            Kind = AssetKind.Geometry,
            DerivedState = null,
            CookStatus = new(this.GeometryUri, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], [], []),
        };
        var material = CreateStatusAsset(1) with
        {
            IdentityUri = this.MaterialUri,
            CookStatus = new(this.MaterialUri, AssetCookFreshness.NeedsCooking, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], [], [])
        };
        _ = this.Assets.SetupGet(value => value.Items).Returns(Observable.Return<IReadOnlyList<ContentBrowserAssetItem>>([this.GeometryAsset, material]));
        _ = this.Assets.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        _ = this.Assets.Setup(value => value.ResolveAsync(this.GeometryUri, It.IsAny<CancellationToken>())).ReturnsAsync(this.GeometryAsset);
        _ = this.Assets.Setup(value => value.ResolveAsync(this.MaterialUri, It.IsAny<CancellationToken>())).ReturnsAsync(material);
        _ = this.pipeline.Setup(value => value.CookPreviewAssetAsync(It.IsAny<Uri>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>())).Returns((Uri uri, ProjectContext project, CancellationToken token) =>
        {
            _ = project.Should().BeSameAs(this.projects.ActiveProject);
            this.Requests.Add((uri, token));
            return new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously).Task.WaitAsync(token);
        });
        this.Service = new(CreateStatusHosting(), this.Assets.Object, this.pipeline.Object, this.projects, this.Authoring.Documents.Object, this.Authoring.Sync.Object, this.Authoring.Messenger, default, NullLogger<SceneContentDemandService>.Instance);
        // Demand assertions run without a visual tree; WaitForRenderAsync only completes while a
        // window produces composition frames, so realize the shared test window up front.
        _ = VisualUserInterfaceTestsApp.MainWindow;
    }

    public SceneAuthoringFixture Authoring { get; } = new();
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
