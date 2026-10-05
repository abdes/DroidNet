// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.Storage.Native;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Exercises the real staged replacement and close guards without keyboard, pointer or window focus.</summary>
[TestClass]
public sealed class DocumentManagerSceneLifetimeTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task OpenSceneAsync_CancelledGuardPreservesAcceptedSceneSelectionAndClipboard(bool cut)
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(false);
        fixture.Participant.PendingEdit = true;
        fixture.PromptDecision = CloseDecision.Cancel;

        var opened = await fixture.Manager.OpenSceneAsync(fixture.Second).ConfigureAwait(false);

        _ = opened.Should().BeFalse();
        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Project.ActiveScene.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.SelectedItemsCount.Should().Be(1);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(cut ? DroidNet.Controls.ClipboardState.Cut : DroidNet.Controls.ClipboardState.Copied);
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        _ = fixture.FirstMetadata.IsDirty.Should().BeTrue();
        _ = fixture.Participant.Resumed.Should().BeTrue();
        _ = SceneAuthoringGate.IsRetired(fixture.First).Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().ContainSingle();
    }

    [TestMethod]
    public async Task OpenSceneAsync_SaveFailurePreservesAcceptedSceneAndCut()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        fixture.Participant.PendingEdit = true;
        fixture.Participant.SaveSucceeds = false;
        fixture.PromptDecision = CloseDecision.Save;

        _ = (await fixture.Manager.OpenSceneAsync(fixture.Second).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Project.ActiveScene.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        _ = fixture.FirstMetadata.IsDirty.Should().BeTrue();
        _ = fixture.Participant.Closed.Should().BeFalse();
    }

    [TestMethod]
    public async Task OpenSceneAsync_CancelledOperationKeepsAcceptedSceneAndCut()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync().ConfigureAwait(false);

        _ = (await fixture.Manager.OpenSceneAsync(fixture.Second, cancellation.Token).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Project.ActiveScene.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Participant.Prepared.Should().BeFalse();
    }

    [TestMethod]
    public async Task OpenSceneAsync_NewerRequestRejectsOlderStagedCompletionBeforeRetirement()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        var third = new Scene(fixture.Project) { Name = "Third" };
        fixture.Project.Scenes.Add(third);
        _ = await fixture.SourceOwner.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(third)).ConfigureAwait(false);
        var staging = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Projects.Setup(value => value.StageSceneLoadAsync(fixture.Second, It.IsAny<CancellationToken>()))
            .Returns(async (Scene scene, CancellationToken token) =>
            {
                staging.SetResult();
                await release.Task.ConfigureAwait(false);
                return await fixture.SourceOwner.StageSceneLoadAsync(scene, token).ConfigureAwait(false);
            });
        var older = fixture.Manager.OpenSceneAsync(fixture.Second);
        await staging.Task.ConfigureAwait(false);
        var newer = fixture.Manager.OpenSceneAsync(third);
        release.SetResult();

        _ = (await older.ConfigureAwait(false)).Should().BeFalse();
        _ = (await newer.ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Explorer.Scene!.AttachedObject.Id.Should().Be(third.Id);
        _ = fixture.Project.ActiveScene!.Id.Should().Be(third.Id);
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).OfType<SceneDocumentMetadata>().Should().ContainSingle().Which.DocumentId.Should().Be(third.Id);
        UndoRedo.GetHistory(third.Id).Clear();
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task OpenSceneAsync_ApprovedReplacementRetiresDifferentIdentityAndAppliesClipboardPolicy(bool cut)
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(false);
        fixture.FirstMetadata.IsDirty = true;
        fixture.PromptDecision = CloseDecision.Discard;
        UndoRedo.GetHistory(fixture.First.Id).AddChange("Old edit", () => Task.CompletedTask);

        _ = (await fixture.Manager.OpenSceneAsync(fixture.Second).ConfigureAwait(false)).Should().BeTrue();

        var accepted = fixture.Explorer.Scene!.AttachedObject;
        _ = accepted.Id.Should().Be(fixture.Second.Id);
        _ = fixture.Project.ActiveScene.Should().BeSameAs(accepted);
        _ = SceneAuthoringGate.IsRetired(fixture.First).Should().BeTrue();
        _ = fixture.Project.Scenes.Single(scene => scene.Id == fixture.First.Id).RootNodes.Should().BeEmpty();
        _ = fixture.Project.Scenes.Should().NotContain(fixture.First);
        _ = fixture.Sync.Object.GetDocumentScene(fixture.FirstMetadata).Should().BeNull();
        _ = UndoRedo.GetHistory(fixture.First.Id).UndoStack.Should().BeEmpty();
        _ = fixture.Explorer.ShownItems.OfType<SceneNodeAdapter>().Should().OnlyContain(adapter => ReferenceEquals(adapter.AttachedObject.Scene, accepted));
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(cut ? DroidNet.Controls.ClipboardState.Empty : DroidNet.Controls.ClipboardState.Copied);
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().Be(!cut);
        _ = fixture.Explorer.ClipboardItems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task OpenSceneAsync_InvalidSourcePreservesDirtySceneAndClipboardBeforeGuard()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        fixture.FirstMetadata.IsDirty = true;
        File.Delete(fixture.SecondPath);

        _ = (await fixture.Manager.OpenSceneAsync(fixture.Second).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Project.ActiveScene.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        _ = fixture.Participant.Prepared.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().ContainSingle();
    }

    [TestMethod]
    public async Task OpenSceneAsync_CopySurvivesGuardedReplacementAndRepeatedPasteUsesSnapshotAndDestinationHistory()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: false).ConfigureAwait(false);
        var source = fixture.First.RootNodes[0];
        source.Name = "Changed after capture";
        source.Components.OfType<TransformComponent>().Single().LocalPosition = new System.Numerics.Vector3(100, 0, 0);

        _ = (await fixture.Manager.OpenSceneAsync(fixture.Second).ConfigureAwait(false)).Should().BeTrue();
        var accepted = fixture.Explorer.Scene!.AttachedObject;
        await fixture.Explorer.PasteItemsAsync(fixture.Explorer.Scene).ConfigureAwait(false);
        await fixture.Explorer.PasteItemsAsync(fixture.Explorer.Scene).ConfigureAwait(false);

        var pasted = accepted.RootNodes.Where(node => node.Name == "Node").ToArray();
        _ = pasted.Should().HaveCount(2);
        _ = pasted.Select(node => node.Id).Should().OnlyHaveUniqueItems().And.NotContain(source.Id);
        _ = pasted.Should().OnlyContain(node => node.Components.OfType<TransformComponent>().Single().LocalPosition == System.Numerics.Vector3.Zero);
        _ = pasted.Should().OnlyContain(node => ReferenceEquals(node.Scene, accepted));
        _ = UndoRedo.GetHistory(fixture.First.Id).UndoStack.Should().BeEmpty();
        _ = UndoRedo.GetHistory(accepted.Id).UndoStack.Should().HaveCount(2);
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).OfType<SceneDocumentMetadata>().Single().IsDirty.Should().BeTrue();
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Copied);
    }

    [TestMethod]
    public async Task OpenMaterialAsync_FocusChangeRetainsAcceptedSceneAndClipboard()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);

        _ = (await fixture.Manager.OpenMaterialAsync(new Uri("asset:///Content/Materials/Test.omat.json"), "Test").ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Project.ActiveScene.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        _ = fixture.Participant.Prepared.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().HaveCount(2);
    }

    [TestMethod]
    public async Task ProjectNotification_SameActivationSettingsRefreshKeepsExplicitSceneAndClipboard()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);

        fixture.Contexts.Activate(ProjectContext.FromProject(fixture.Project) with { DefaultSceneId = fixture.Second.Id });

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Project.ActiveScene.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeTrue();
    }

    [TestMethod]
    public async Task OpenSceneAsync_FailureAfterRetirementShowsUnavailableStateAndCanReloadPreviousSavedScene()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        _ = fixture.Sync.Setup(value => value.RegisterDocument(It.Is<Scene>(scene => scene.Id == fixture.Second.Id), It.IsAny<SceneDocumentMetadata>()))
            .Returns(value: false);

        _ = (await fixture.Manager.OpenSceneAsync(fixture.Second).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Explorer.Scene.Should().BeNull();
        _ = fixture.Explorer.ShownItems.Should().BeEmpty();
        _ = fixture.Project.ActiveScene.Should().BeNull();
        _ = fixture.Project.ActiveScene.Should().BeNull();
        _ = fixture.Explorer.HasUnavailableScene.Should().BeTrue();
        _ = fixture.Explorer.ReloadPreviousSceneCommand.CanExecute(null).Should().BeTrue();
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeFalse();
        await fixture.Explorer.ReloadPreviousSceneCommand.ExecuteAsync(null).ConfigureAwait(false);
        _ = fixture.Explorer.Scene!.AttachedObject.Id.Should().Be(fixture.First.Id);
        _ = fixture.Explorer.Scene.AttachedObject.Should().NotBeSameAs(fixture.First);
        _ = fixture.Explorer.HasUnavailableScene.Should().BeFalse();
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Empty);
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ProjectReplacement_CancelKeepsClipboardAndSuccessfulActivationClearsItWithoutLoadingDefault(bool cut)
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(false);
        fixture.Participant.PendingEdit = true;
        fixture.PromptDecision = CloseDecision.Cancel;
        using (var cancelled = await fixture.Documents.PrepareCloseAllWindowsAsync().ConfigureAwait(false))
        {
            _ = cancelled.Should().BeNull();
        }

        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeTrue();
        _ = fixture.Contexts.ActiveProject!.ProjectId.Should().Be(fixture.Project.ProjectInfo.Id);
        fixture.PromptDecision = CloseDecision.Discard;
        using (var approved = await fixture.Documents.PrepareCloseAllWindowsAsync().ConfigureAwait(false))
        {
            _ = approved.Should().NotBeNull();
            _ = (await approved!.CommitAsync().ConfigureAwait(false)).Should().BeTrue();
        }

        var next = new Project(new ProjectInfo("Next", Category.Games, fixture.Root, "preview.png")) { Name = "Next" };
        fixture.Contexts.Activate(ProjectContext.FromProject(next));

        _ = fixture.Explorer.Scene.Should().BeNull();
        _ = fixture.Explorer.ShownItems.Should().BeEmpty();
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Empty);
        _ = fixture.Explorer.PasteCommand.CanExecute(null).Should().BeFalse();
    }

    private enum CloseDecision
    {
        Cancel,
        Discard,
        Save,
    }

    private sealed class LifetimeFixture : IDisposable
    {
        private readonly Dictionary<SceneDocumentMetadata, Scene> registrations = [];
        private readonly StrongReferenceMessenger messenger = new();

        private LifetimeFixture()
        {
            this.Root = Path.Combine(AppContext.BaseDirectory, "SceneLifetimeTests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
            this.Project = new Project(new ProjectInfo("Lifetime", Category.Games, this.Root, "preview.png")) { Name = "Lifetime" };
            this.First = new Scene(this.Project) { Name = "First" };
            this.First.RootNodes.Add(new SceneNode(this.First) { Name = "Node" });
            this.Second = new Scene(this.Project) { Name = "Second" };
            this.Second.RootNodes.Add(new SceneNode(this.Second) { Name = "Destination" });
            this.Project.Scenes.Add(this.First);
            this.Project.Scenes.Add(this.Second);
            this.Project.ActiveScene = this.First;
            this.FirstMetadata = new(this.First.Id) { Title = this.First.Name };
            this.Contexts.Activate(ProjectContext.FromProject(this.Project));
            var owner = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
            var projects = new Mock<IProjectManagerService>();
            this.Projects = projects;
            _ = projects.SetupGet(value => value.CurrentProject).Returns(this.Project);
            _ = projects.Setup(value => value.StageSceneLoadAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>()))
                .Returns((Scene scene, CancellationToken token) => owner.StageSceneLoadAsync(scene, token));
            _ = projects.Setup(value => value.AcceptSceneLoad(It.IsAny<SceneLoadSnapshot>()))
                .Returns((SceneLoadSnapshot snapshot) => owner.AcceptSceneLoad(snapshot));
            _ = projects.Setup(value => value.RetireScene(It.IsAny<Scene>())).Callback<Scene>(owner.RetireScene);
            this.SourceOwner = owner;
            _ = this.Sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), It.IsAny<SceneDocumentMetadata>()))
                .Returns((Scene scene, SceneDocumentMetadata metadata) =>
                {
                    this.registrations[metadata] = scene;
                    return true;
                });
            _ = this.Sync.Setup(value => value.GetDocumentScene(It.IsAny<SceneDocumentMetadata>()))
                .Returns((SceneDocumentMetadata metadata) => this.registrations.GetValueOrDefault(metadata));
            _ = this.Sync.Setup(value => value.CloseDocument(It.IsAny<SceneDocumentMetadata>()))
                .Callback<SceneDocumentMetadata>(metadata => this.registrations.Remove(metadata));
            _ = this.Sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
            var prompt = new Mock<IDocumentClosePrompt>();
            _ = prompt.Setup(value => value.ConfirmAsync(It.IsAny<WindowId>(), It.IsAny<IReadOnlyList<DocumentCloseItem>>(), It.IsAny<bool>()))
                .Returns(async (WindowId _, IReadOnlyList<DocumentCloseItem> items, bool _) =>
                {
                    if (this.PromptDecision == CloseDecision.Cancel)
                    {
                        return false;
                    }

                    foreach (var item in items)
                    {
                        item.IsSelected = this.PromptDecision == CloseDecision.Save;
                        if (item.IsSelected)
                        {
                            _ = await item.SaveAsync().ConfigureAwait(false);
                        }
                    }

                    return true;
                });
            var close = new DocumentCloseCoordinator(prompt.Object, Mock.Of<IOperationResultPublisher>());
            this.Documents = new EditorDocumentService(closeCoordinator: close);
            this.Participant = new SceneParticipant(this);
            close.Register(this.Window, this.First.Id, this.Participant);
            var selection = new SceneSelectionService();
            var commands = new SceneDocumentCommandService(
                Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), selection, this.Sync.Object,
                projects.Object, this.Documents, this.Window, this.messenger, Mock.Of<IOperationResultPublisher>(),
                new OperationStatusReducer(), Mock.Of<Oxygen.Editor.ContentPipeline.Inspection.IGeometryMaterialSlotProvider>(),
                this.Contexts, new SceneMutator(NullLogger<SceneMutator>.Instance), new SceneOrganizer(NullLogger<SceneOrganizer>.Instance));
            this.Explorer = new SceneExplorerViewModel(
                projects.Object, this.messenger, Mock.Of<IRouter>(), this.Documents, this.Window, this.Sync.Object,
                selection, commands, projectContexts: this.Contexts);
            this.Manager = new DocumentManager(
                this.Documents, this.messenger, this.Contexts, Mock.Of<IProjectUsageService>(),
                Mock.Of<IMaterialDocumentService>(), this.Window, projects.Object, this.Sync.Object);
        }

        public string Root { get; }

        public WindowId Window { get; } = new(73);

        public Project Project { get; }

        public Scene First { get; }

        public Scene Second { get; }

        public SceneDocumentMetadata FirstMetadata { get; }

        public ProjectContextService Contexts { get; } = new();

        public Mock<ISceneEngineSync> Sync { get; } = new();

        public EditorDocumentService Documents { get; }

        public SceneExplorerViewModel Explorer { get; }

        public DocumentManager Manager { get; }

        public SceneParticipant Participant { get; }

        public CloseDecision PromptDecision { get; set; } = CloseDecision.Discard;

        public ProjectManagerService SourceOwner { get; }

        public Mock<IProjectManagerService> Projects { get; }

        public string SecondPath => Path.Combine(this.Root, "Content", "Scenes", this.Second.Name + Oxygen.Editor.Projects.Constants.SceneFileExtension);

        public static async Task<LifetimeFixture> CreateAsync(bool cut)
        {
            var fixture = new LifetimeFixture();
            try
            {
                _ = await fixture.SourceOwner.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(fixture.Second)).ConfigureAwait(false);
                _ = await fixture.SourceOwner.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(fixture.First)).ConfigureAwait(false);
                _ = fixture.Sync.Object.RegisterDocument(fixture.First, fixture.FirstMetadata);
                _ = await fixture.Documents.OpenDocumentAsync(fixture.Window, fixture.FirstMetadata, shouldSelect: false).ConfigureAwait(false);
                await fixture.Explorer.HandleDocumentOpenedAsync(fixture.First).ConfigureAwait(false);
                _ = await fixture.Documents.SelectDocumentAsync(fixture.Window, fixture.First.Id).ConfigureAwait(false);
                var adapter = await fixture.Explorer.FindAdapterByNodeIdAsync(fixture.First.RootNodes[0].Id).ConfigureAwait(false);
                fixture.Explorer.SelectDisplayedItem(adapter!, fixture.Explorer.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
                if (cut)
                {
                    await fixture.Explorer.CutItemsAsync([adapter!]).ConfigureAwait(false);
                }
                else
                {
                    await fixture.Explorer.CopyItemsAsync([adapter!]).ConfigureAwait(false);
                }

                return fixture;
            }
            catch
            {
                fixture.Dispose();
                throw;
            }
        }

        public void Dispose()
        {
            this.Manager.Dispose();
            this.Explorer.Dispose();
            UndoRedo.GetHistory(this.First.Id).Clear();
            UndoRedo.GetHistory(this.Second.Id).Clear();
            Directory.Delete(this.Root, recursive: true);
        }
    }

    private sealed class SceneParticipant(LifetimeFixture fixture) : IDocumentCloseParticipant
    {
        public bool PendingEdit { get; set; }

        public bool SaveSucceeds { get; set; } = true;

        public bool Prepared { get; private set; }

        public bool Resumed { get; private set; }

        public bool Closed { get; private set; }

        public Task PrepareForCloseAsync()
        {
            this.Prepared = true;
            if (this.PendingEdit)
            {
                fixture.FirstMetadata.IsDirty = true;
                this.PendingEdit = false;
            }

            return Task.CompletedTask;
        }

        public Task<bool> SaveForCloseAsync()
        {
            if (this.SaveSucceeds)
            {
                fixture.FirstMetadata.IsDirty = false;
            }

            return Task.FromResult(this.SaveSucceeds);
        }

        public Task CloseAsync(bool discard)
        {
            this.Closed = true;
            fixture.Sync.Object.CloseDocument(fixture.FirstMetadata);
            SceneAuthoringGate.Retire(fixture.First);
            UndoRedo.GetHistory(fixture.First.Id).Clear();
            fixture.SourceOwner.RetireScene(fixture.First);
            return Task.CompletedTask;
        }

        public void ResumeEditing() => this.Resumed = true;
    }
}
