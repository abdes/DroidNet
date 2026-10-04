// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed partial class SceneAuthoringFixture : IDisposable
{
    public SceneAuthoringFixture()
    {
        var info = new ProjectInfo("UI slot fixtures", Category.Games, Path.Combine(Path.GetTempPath(), "Oxygen-UI-Slots"));
        var project = new Mock<IProject>();
        _ = project.SetupGet(value => value.ProjectInfo).Returns(info);
        this.Projects.Activate(ProjectContext.FromProjectInfo(info));
        _ = this.Slots.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<Uri>(), It.IsAny<CancellationToken>())).ReturnsAsync((ProjectContext _, Uri uri, CancellationToken _) => new GeometryMaterialSlotMetadata(uri, Guid.Parse("20000000-0000-0000-0000-000000000001"), new string('a', 64), [new(this.TargetFor(uri).SlotId, "Surface", [new(0, 0, Guid.Empty)])]));
        this.Scene = new Scene(project.Object)
        {
            Name = "UI Test Scene"
        };
        this.Node = new SceneNode(this.Scene)
        {
            Name = "Camera and sun"
        };
        this.Camera = new PerspectiveCamera
        {
            Name = "Camera",
            NearPlane = 0.1f,
            FarPlane = 1000
        };
        _ = this.Node.AddComponent(this.Camera);
        _ = this.Node.AddComponent(new DirectionalLightComponent { Name = "Sun" });
        this.Scene.RootNodes.Add(this.Node);
        this.Context = new(this.Scene.Id, new SceneDocumentMetadata(this.Scene.Id), this.Scene, UndoRedo.GetHistory(this.Scene.Id));
        var sync = this.Sync;
        var accepted = new SyncOutcome(SyncStatus.Accepted, "UI control command", AffectedScope.Empty);
        _ = sync.Setup(value => value.UpdateMaterialSlotAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<MaterialSlotTarget>(), It.IsAny<Uri?>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        _ = sync.Setup(value => value.RestoreMaterialSlotAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<MaterialSlotTarget>(), It.IsAny<Uri?>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        _ = sync.Setup(value => value.UpdatePropertiesAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>())).ReturnsAsync(accepted);
        _ = sync.Setup(value => value.UpdateEnvironmentAsync(It.IsAny<Scene>(), It.IsAny<SceneEnvironmentData>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>())).ReturnsAsync(new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncOutcome>(StringComparer.Ordinal)));
        _ = sync.Setup(value => value.TryPreviewSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<DateTimeOffset>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>())).Returns(async (Guid _, Guid _, DateTimeOffset _, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) => (SyncOutcome?)await action(token).ConfigureAwait(true));
        _ = sync.Setup(value => value.CompleteTerminalSyncAsync(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>())).Returns((Guid _, Guid _, Func<CancellationToken, Task<SyncOutcome>> action, CancellationToken token) => action(token));
        var documents = this.Documents;
        _ = documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).ReturnsAsync(value: true);
        this.Commands = new SceneDocumentCommandService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new SceneSelectionService(), sync.Object, Mock.Of<IProjectManagerService>(), documents.Object, default, this.Messenger, Mock.Of<IOperationResultPublisher>(), new OperationStatusReducer(), this.Slots.Object, this.Projects, Mock.Of<ISceneMutator>(), Mock.Of<ISceneOrganizer>());
    }

    public ProjectContextService Projects { get; } = new();
    public Mock<IGeometryMaterialSlotProvider> Slots { get; } = new();

    public MaterialSlotTarget TargetFor(Uri geometry) => new(geometry, Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));
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
