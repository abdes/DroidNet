// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Uses real authoring commands with a managed sync substitute, without claiming native presentation.</summary>
public sealed partial class InspectorControlTests
{
    private static MaterialEditorViewModel CreateMaterialEditor(Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider? assetProvider = null)
    {
        var uri = new Uri("asset:///Content/Materials/UI.omat.json");
        var source = new MaterialSource(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: "UI",
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(1, 1, 1, 1, 0, 0.5f, baseColorTexture: null, metallicRoughnessTexture: null),
            normalTexture: null,
            occlusionTexture: null,
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);
        var document = new MaterialDocument(Guid.NewGuid(), uri, Guid.NewGuid(), "C:/UI.omat.json", "UI", source, new MaterialAsset { Uri = uri, Source = source }, IsDirty: false, MaterialCookState.NotCooked);
        var service = new Mock<IMaterialDocumentService>();
        _ = service.Setup(value => value.OpenAsync(uri, It.IsAny<CancellationToken>())).ReturnsAsync(document);
        _ = service.Setup(value => value.GetDocument(document.DocumentId)).Returns(document);
        return new(new MaterialDocumentMetadata(uri), service.Object, assetProvider ?? Oxygen.Testing.AssetStatusFixture.EmptyProvider, CreateStatusHosting().DispatcherScheduler);
    }

    private sealed partial class Fixture : IDisposable
    {
        public Fixture()
        {
            this.Scene = new Scene(Mock.Of<IProject>()) { Name = "UI Test Scene" };
            this.Node = new SceneNode(this.Scene) { Name = "Camera and sun" };
            this.Camera = new PerspectiveCamera { Name = "Camera", NearPlane = 0.1f, FarPlane = 1000 };
            _ = this.Node.AddComponent(this.Camera);
            _ = this.Node.AddComponent(new DirectionalLightComponent { Name = "Sun" });
            this.Scene.RootNodes.Add(this.Node);
            this.Context = new(this.Scene.Id, new SceneDocumentMetadata(this.Scene.Id), this.Scene, UndoRedo.GetHistory(this.Scene.Id));
            var sync = this.Sync;
            var accepted = new SyncOutcome(SyncStatus.Accepted, "UI control command", AffectedScope.Empty);
            _ = sync.Setup(value => value.UpdatePropertiesAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
            _ = sync.Setup(value => value.UpdateEnvironmentAsync(It.IsAny<Scene>(), It.IsAny<SceneEnvironmentData>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync(new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncOutcome>(StringComparer.Ordinal)));
            _ = sync.Setup(value => value.TryPreviewSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<DateTimeOffset>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
                .Returns(async (Guid _, Guid _, DateTimeOffset _, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) => (SyncOutcome?)await action(token).ConfigureAwait(true));
            _ = sync.Setup(value => value.CompleteTerminalSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
                .Returns((Guid _, Guid _, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) => action(token));
            var documents = this.Documents;
            _ = documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).ReturnsAsync(value: true);
            this.Commands = new SceneDocumentCommandService(
                Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
                Mock.Of<ISceneExplorerService>(),
                new SceneSelectionService(),
                sync.Object,
                Mock.Of<IProjectManagerService>(),
                documents.Object,
                default,
                this.Messenger,
                Mock.Of<IOperationResultPublisher>(),
                new OperationStatusReducer());
        }

        public Scene Scene { get; }

        public SceneNode Node { get; }

        public PerspectiveCamera Camera { get; }

        public SceneDocumentCommandContext Context { get; }

        public SceneDocumentCommandService Commands { get; }

        public Mock<ISceneEngineSync> Sync { get; } = new();

        public Mock<IDocumentService> Documents { get; } = new();

        public StrongReferenceMessenger Messenger { get; } = new();

        public void Dispose() => this.Context.History.Clear();
    }
}
