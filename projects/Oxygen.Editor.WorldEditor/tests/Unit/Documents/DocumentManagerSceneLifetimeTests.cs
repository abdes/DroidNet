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
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Exercises the real staged replacement and close guards without keyboard, pointer or window focus.</summary>
[TestClass]
public sealed partial class DocumentManagerSceneLifetimeTests
{
    private enum CloseDecision
    {
        Cancel,
        Discard,
        Save,
    }

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task OpenSceneAsync_CancelledGuardPreservesAcceptedSceneSelectionAndClipboard(bool cut)
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(false);
        fixture.Participant.PendingEdit = true;
        fixture.PromptDecision = CloseDecision.Cancel;

        var opened = await fixture.Manager.OpenSceneAsync(fixture.SecondUri, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = opened.Should().BeFalse();
        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.SelectedItemsCount.Should().Be(1);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(cut ? DroidNet.Controls.ClipboardState.Cut : DroidNet.Controls.ClipboardState.Copied);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.FirstDocument.IsDirty.Should().BeTrue();
        _ = fixture.Participant.Resumed.Should().BeTrue();
        _ = ((SceneContent)fixture.FirstDocument.Content).Authoring.IsRetired.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().ContainSingle();
        _ = fixture.Files.Documents.Documents.Should().Equal(fixture.FirstDocument);
    }

    [TestMethod]
    public async Task OpenSceneAsync_SaveFailurePreservesAcceptedSceneAndCut()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        fixture.Participant.PendingEdit = true;
        fixture.Participant.SaveSucceeds = false;
        fixture.PromptDecision = CloseDecision.Save;

        _ = (await fixture.Manager.OpenSceneAsync(fixture.SecondUri, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.FirstDocument.IsDirty.Should().BeTrue();
        _ = fixture.Participant.Closed.Should().BeFalse();
    }

    [TestMethod]
    public async Task OpenSceneAsync_CancelledOperationKeepsAcceptedSceneAndCut()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync().ConfigureAwait(false);

        _ = (await fixture.Manager.OpenSceneAsync(fixture.SecondUri, cancellation.Token).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Participant.Prepared.Should().BeFalse();
    }

    [TestMethod]
    public async Task OpenSceneAsync_NewerRequestRejectsOlderReadBeforeRetirement()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        fixture.Files.Write("Third", SceneDocumentFiles.Data(SceneId.New(), "Third"));
        var reading = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Store.BeforeRead = async path =>
        {
            if (string.Equals(path, fixture.Files.PathOf("Second"), StringComparison.OrdinalIgnoreCase))
            {
                reading.SetResult();
                await release.Task.ConfigureAwait(false);
            }
        };
        var older = fixture.Manager.OpenSceneAsync(fixture.SecondUri, this.TestContext.CancellationToken);
        await reading.Task.ConfigureAwait(false);
        var newer = fixture.Manager.OpenSceneAsync(fixture.Files.FileOf("Third").AssetUri, this.TestContext.CancellationToken);
        release.SetResult();

        _ = (await older.ConfigureAwait(false)).Should().BeFalse();
        _ = (await newer.ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Explorer.Scene!.AttachedObject.Name.Should().Be("Third");
        _ = fixture.Area.Documents.Should().ContainSingle()
            .Which.File.Path.Should().Be(fixture.Files.PathOf("Third"));
        _ = fixture.Files.Documents.Documents.Should().ContainSingle("the older request closed the document it read");
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task OpenSceneAsync_ApprovedReplacementRetiresThePreviousSceneAndAppliesClipboardPolicy(bool cut)
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(false);
        fixture.FirstDocument.History.AddChange("Old edit", static () => { });
        fixture.PromptDecision = CloseDecision.Discard;

        _ = (await fixture.Manager.OpenSceneAsync(fixture.SecondUri, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var accepted = fixture.Explorer.Scene!.AttachedObject;
        _ = accepted.Name.Should().Be("Second");
        _ = ((SceneContent)fixture.FirstDocument.Content).Authoring.IsRetired.Should().BeTrue();
        _ = fixture.Files.Documents.Documents.Should().ContainSingle().Which.File.Path.Should().Be(fixture.Files.PathOf("Second"));
        _ = fixture.Explorer.ShownItems.OfType<SceneNodeAdapter>().Should().OnlyContain(adapter => ReferenceEquals(adapter.AttachedObject.Scene, accepted));
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(cut ? DroidNet.Controls.ClipboardState.Empty : DroidNet.Controls.ClipboardState.Copied);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().Be(!cut);
        _ = fixture.Explorer.ClipboardItems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task OpenSceneAsync_InvalidSourceAbandonsThePreparedCloseAndKeepsDirtySceneAndClipboard()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);
        fixture.FirstDocument.History.AddChange("Edit", static () => { });
        File.Delete(fixture.Files.PathOf("Second"));

        _ = (await fixture.Manager.OpenSceneAsync(fixture.SecondUri, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.Participant.Prepared.Should().BeTrue("the unsaved edits prompt comes before the incoming scene is read");
        _ = fixture.Participant.Resumed.Should().BeTrue("a scene that does not open abandons the prepared close");
        _ = fixture.Participant.Closed.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().ContainSingle();
        _ = fixture.FirstDocument.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task OpenSceneAsync_CopySurvivesGuardedReplacementAndRepeatedPasteUsesSnapshotAndDestinationHistory()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: false).ConfigureAwait(false);
        var source = fixture.First.RootNodes[0];
        source.Name = "Changed after capture";
        source.Components.OfType<TransformComponent>().Single().LocalPosition = new System.Numerics.Vector3(100, 0, 0);

        _ = (await fixture.Manager.OpenSceneAsync(fixture.SecondUri, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        var accepted = fixture.Explorer.Scene!.AttachedObject;
        await fixture.Explorer.PasteItemsAsync(fixture.Explorer.Scene).ConfigureAwait(false);
        await fixture.Explorer.PasteItemsAsync(fixture.Explorer.Scene).ConfigureAwait(false);

        var pasted = accepted.RootNodes.Where(node => string.Equals(node.Name, "Node", StringComparison.Ordinal)).ToArray();
        _ = pasted.Should().HaveCount(2);
        _ = pasted.Should().OnlyHaveUniqueItems(node => node.Id);
        _ = pasted.Should().NotContain(node => node.Id == source.Id);
        _ = pasted.Should().OnlyContain(node => node.Components.OfType<TransformComponent>().Single().LocalPosition == System.Numerics.Vector3.Zero);
        _ = pasted.Should().OnlyContain(node => ReferenceEquals(node.Scene, accepted));
        var destination = fixture.Files.Documents.Find(fixture.Files.PathOf("Second"))!;
        _ = destination.History.UndoStack.Should().HaveCount(2);
        _ = destination.IsDirty.Should().BeTrue();
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Copied);
    }

    [TestMethod]
    public async Task OpenMaterialAsync_FocusChangeRetainsAcceptedSceneAndClipboard()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);

        _ = (await fixture.Manager.OpenMaterialAsync(fixture.Files.WriteMaterial("Test").AssetUri, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.Participant.Prepared.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().HaveCount(2);
    }

    [TestMethod]
    public async Task ProjectNotification_SameActivationSettingsRefreshKeepsExplicitSceneAndClipboard()
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(false);

        fixture.Contexts.Activate(fixture.Files.Context with { DefaultSceneId = SceneId.New() });

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
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

        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.Contexts.ActiveProject!.ProjectId.Should().Be(fixture.Files.Context.ProjectId);
        fixture.PromptDecision = CloseDecision.Discard;
        using (var approved = await fixture.Documents.PrepareCloseAllWindowsAsync().ConfigureAwait(false))
        {
            _ = approved.Should().NotBeNull();
            _ = (await approved!.CommitAsync().ConfigureAwait(false)).Should().BeTrue();
        }

        fixture.Contexts.Activate(fixture.Files.Context with { ProjectId = Guid.NewGuid(), Name = "Next" });

        _ = fixture.Explorer.Scene.Should().BeNull();
        _ = fixture.Explorer.ShownItems.Should().BeEmpty();
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Empty);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeFalse();
    }

    /// <summary>
    /// A workspace with the scene "First" open and one of its nodes selected and copied or cut, and the scene "Second"
    /// saved beside it.
    /// </summary>
    private sealed partial class LifetimeFixture : IDisposable
    {
        private readonly StrongReferenceMessenger messenger = new();
        private readonly DocumentCloseCoordinator close;

        private LifetimeFixture()
        {
            this.close = new DocumentCloseCoordinator(this.CreateClosePrompt(), Mock.Of<IOperationResultPublisher>());
            this.Documents = new EditorDocumentService(closeCoordinator: this.close);
            var selection = new SceneSelectionService();
            this.Files = new SceneDocumentFiles(this.Store);
            this.Contexts.Activate(this.Files.Context);
            this.Files.Write("First", SceneDocumentFiles.Data(SceneId.New(), "First", "Node"));
            this.Files.Write("Second", SceneDocumentFiles.Data(SceneId.New(), "Second", "Destination"));
            this.Area = new DocumentArea(
                this.Documents,
                this.Files.Documents,
                this.Files.Documents.Kinds.Select(static kind => new DocumentEditorDescriptor(kind, static _ => new object())),
                this.Window,
                static _ => { });
            this.Explorer = new SceneExplorerViewModel(
                this.Files.ProjectManager,
                this.messenger,
                Mock.Of<IRouter>(),
                this.Area,
                this.Files.Documents,
                this.Window,
                selection,
                projectContexts: this.Contexts);
            this.Manager = new DocumentManager(
                this.Area,
                this.messenger,
                this.Contexts,
                Mock.Of<IProjectUsageService>(),
                this.Window,
                this.Files.Documents,
                Mock.Of<ContentPipeline.Relocation.IAssetRelocationService>());
            this.Participant = new SceneParticipant(this);
        }

        public WindowId Window { get; } = new(73);

        public GatedFileStore Store { get; } = new(new NativeAtomicFileStore(new RealFileSystem()));

        public SceneDocumentFiles Files { get; }

        public Uri SecondUri => this.Files.FileOf("Second").AssetUri;

        public Document FirstDocument { get; private set; } = null!;

        public Scene First => ((SceneContent)this.FirstDocument.Content).Scene;

        public ProjectContextService Contexts { get; } = new();

        public EditorDocumentService Documents { get; }

        public DocumentArea Area { get; }

        public SceneExplorerViewModel Explorer { get; }

        public DocumentManager Manager { get; }

        public SceneParticipant Participant { get; }

        public CloseDecision PromptDecision { get; set; } = CloseDecision.Discard;

        public static async Task<LifetimeFixture> CreateAsync(bool cut)
        {
            var fixture = new LifetimeFixture();
            try
            {
                _ = (await fixture.Manager.OpenSceneAsync(fixture.Files.FileOf("First").AssetUri).ConfigureAwait(false)).Should().BeTrue();
                fixture.FirstDocument = fixture.Files.Documents.Find(fixture.Files.PathOf("First"))!;
                fixture.close.Register(fixture.Window, fixture.FirstDocument.Id, fixture.Participant);
                var adapter = await fixture.Explorer.FindAdapterByNodeIdAsync(fixture.First.RootNodes[0].Id).ConfigureAwait(false);
                fixture.Explorer.SelectDisplayedItem(adapter!, [.. fixture.Explorer.ShownItems], isControlDown: false, isShiftDown: false);
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
            this.Area.Dispose();
            this.Files.Dispose();
        }

        // The close prompt answers with the decision the test set: cancel, or save or discard every document it lists.
        private IDocumentClosePrompt CreateClosePrompt()
        {
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
            return prompt.Object;
        }
    }

    /// <summary>Stands in for the scene editor at close: it finishes, saves or discards the first scene's edits.</summary>
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
                fixture.FirstDocument.History.AddChange("Pending edit", static () => { });
                this.PendingEdit = false;
            }

            return Task.CompletedTask;
        }

        public async Task<bool> SaveForCloseAsync()
            => this.SaveSucceeds && (await fixture.FirstDocument.SaveAsync().ConfigureAwait(false)).Succeeded;

        public Task CloseAsync(bool discard)
        {
            this.Closed = true;
            fixture.Files.Documents.Close(fixture.FirstDocument);
            return Task.CompletedTask;
        }

        public void ResumeEditing() => this.Resumed = true;
    }
}
