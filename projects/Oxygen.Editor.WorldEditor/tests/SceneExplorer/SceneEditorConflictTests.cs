// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DryIoc;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Verifies scene conflict actions through the real document view model.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed partial class SceneEditorConflictTests
{
    /// <summary>Close recovery uses inline actions, replaces the model and refreshes only the active document.</summary>
    /// <param name="active">Whether this scene owns the active editor surface.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task CloseConflictReloadRebindsTheCorrectDocument(bool active)
    {
        using var fixture = new Fixture(active);
        var notifications = 0;
        fixture.Messenger.Register<SceneReloadedMessage>(this, (_, message) =>
        {
            notifications++;
            _ = message.Scene.Should().BeSameAs(fixture.Replacement);
            message.Reply(Task.FromResult(true));
        });
        await fixture.Editor.PrepareForCloseAsync().ConfigureAwait(false);
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = fixture.Editor.HasSaveConflict.Should().BeTrue();

        var reload = await fixture.Editor.ReloadFromDiskAsync().ConfigureAwait(false);

        _ = reload.Succeeded.Should().BeTrue();
        _ = fixture.Editor.HasSaveConflict.Should().BeFalse();
        _ = notifications.Should().Be(active ? 1 : 0);
        fixture.Prompt.Verify(value => value.ShowAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<IDocumentConflictParticipant>()), Times.Never);
        fixture.Editor.ResumeEditing();
        _ = await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false);
        fixture.Commands.Verify(value => value.SaveSceneAsync(It.Is<SceneDocumentCommandContext>(context => ReferenceEquals(context.Scene, fixture.Replacement))), Times.Once);
    }

    /// <summary>Saving a copy does not redirect or acknowledge the original scene.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SceneCopyRetainsOriginalIdentityDirtyStateAndConflict()
    {
        using var fixture = new Fixture(active: true);
        _ = await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false);
        _ = fixture.Commands.Setup(value => value.SaveSceneCopyAsync(It.IsAny<SceneDocumentCommandContext>(), "Copy"))
            .ReturnsAsync(SceneCommandResults.Success(new Scene(fixture.Scene.Project) { Name = "Copy" }));

        _ = (await fixture.Editor.SaveCopyAsync("Copy").ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = fixture.Metadata.IsDirty.Should().BeTrue();
        _ = fixture.Editor.HasSaveConflict.Should().BeTrue();
        _ = fixture.Editor.Metadata.Should().BeSameAs(fixture.Metadata);
        _ = await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false);
        fixture.Commands.Verify(value => value.SaveSceneAsync(It.Is<SceneDocumentCommandContext>(context => ReferenceEquals(context.Scene, fixture.Scene))), Times.Exactly(2));
    }

    /// <summary>A close request waits for already-submitted reload work before retiring the document.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CloseWaitsForPendingSceneRecovery()
    {
        using var fixture = new Fixture(active: false);
        var loaded = new TaskCompletionSource<SceneValueCommandResult<Scene>>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Commands.Setup(value => value.ReloadSceneAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<CancellationToken>())).Returns(loaded.Task);
        var reload = fixture.Editor.ReloadFromDiskAsync();
        var close = fixture.Editor.CloseAsync(discard: true);

        _ = close.IsCompleted.Should().BeFalse();
        fixture.Sync.Verify(value => value.CloseDocument(fixture.Metadata), Times.Never);
        fixture.Metadata.IsDirty = false;
        loaded.SetResult(SceneCommandResults.Success(fixture.Replacement));
        _ = (await reload.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await close.ConfigureAwait(false);
        fixture.Sync.Verify(value => value.CloseDocument(fixture.Metadata), Times.Once);
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly Container container = new();

        public Fixture(bool active)
        {
            this.Scene = new Scene(Mock.Of<IProject>()) { Name = "Main" };
            this.Replacement = Scene.CreateAndHydrate(this.Scene.Project, this.Scene.Dehydrate());
            this.Metadata = new(this.Scene.Id) { IsDirty = true };
            this.Sync = new();
            _ = this.Sync.Setup(value => value.GetDocumentScene(this.Metadata)).Returns(this.Scene);
            this.Commands = new();
            _ = this.Commands.Setup(value => value.CompleteEditSessionsAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
            _ = this.Commands.Setup(value => value.SaveSceneAsync(It.IsAny<SceneDocumentCommandContext>())).ReturnsAsync(new SceneCommandResult(Succeeded: false) { IsConflict = true });
            _ = this.Commands.Setup(value => value.ReloadSceneAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<CancellationToken>())).Returns(() =>
            {
                this.Metadata.IsDirty = false;
                return Task.FromResult(SceneCommandResults.Success(this.Replacement));
            });
            this.Prompt = new();
            _ = this.Prompt.Setup(value => value.ShowAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<IDocumentConflictParticipant>())).Returns(Task.CompletedTask);
            var documents = new Mock<IDocumentService>();
            _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(active ? this.Metadata.DocumentId : Guid.NewGuid());
            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync(It.IsAny<WindowId>())).Returns(Task.CompletedTask);
            this.Editor = new(
                this.Metadata,
                documents.Object,
                default,
                Mock.Of<IEngineService>(),
                this.Sync.Object,
                input.Object,
                Mock.Of<IOperationResultPublisher>(),
                new OperationStatusReducer(),
                this.Commands.Object,
                Mock.Of<IContentPipelineService>(),
                Mock.Of<IContentBrowserAssetProvider>(),
                this.container,
                this.Messenger,
                new SceneCookInputRegistrar(
                    new Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry(),
                    Mock.Of<IProjectManagerService>(),
                    new DroidNet.Hosting.WinUI.HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! }),
                conflictPrompt: this.Prompt.Object);
        }

        public Scene Scene { get; }

        public Scene Replacement { get; }

        public SceneDocumentMetadata Metadata { get; }

        public SceneEditorViewModel Editor { get; }

        public StrongReferenceMessenger Messenger { get; } = new();

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public Mock<IDocumentConflictPrompt> Prompt { get; }

        public void Dispose()
        {
            this.Editor.Dispose();
            this.Messenger.Reset();
            this.container.Dispose();
        }
    }
}
