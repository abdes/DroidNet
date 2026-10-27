// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Tests;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneExplorer.Unit.UI.Tests;

/// <summary>
/// Exercises the Scene Explorer while the window's document area replaces its scene, with the session's close guards:
/// the explorer keeps its scene, selection and clipboard until a replacement is accepted, without keyboard, pointer or
/// window focus.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed partial class SceneExplorerReplacementTests : VisualUserInterfaceTests
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
    public Task OpenSceneAsync_CancelledGuardPreservesAcceptedSceneSelectionAndClipboard(bool cut) => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(true);
        fixture.Participant.PendingEdit = true;
        fixture.PromptDecision = CloseDecision.Cancel;

        var opened = await fixture.OpenAsync(fixture.SecondAsset, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = opened.Should().BeFalse();
        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.SelectedItemsCount.Should().Be(1);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(cut ? DroidNet.Controls.ClipboardState.Cut : DroidNet.Controls.ClipboardState.Copied);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.FirstDocument.IsDirty.Should().BeTrue();
        _ = fixture.Participant.Resumed.Should().BeTrue();
        _ = ((SceneContent)fixture.FirstDocument.Content).Authoring.IsRetired.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().ContainSingle();
        _ = fixture.Scenes.Open.Documents.Should().Equal(fixture.FirstDocument);
    });

    [TestMethod]
    public Task OpenSceneAsync_SaveFailurePreservesAcceptedSceneAndCut() => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(true);
        fixture.Participant.PendingEdit = true;
        fixture.Participant.SaveSucceeds = false;
        fixture.PromptDecision = CloseDecision.Save;

        _ = (await fixture.OpenAsync(fixture.SecondAsset, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().BeFalse();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.FirstDocument.IsDirty.Should().BeTrue();
        _ = fixture.Participant.Closed.Should().BeFalse();
    });

    [TestMethod]
    public Task OpenSceneAsync_CancelledOperationKeepsAcceptedSceneAndCut() => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(true);
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync().ConfigureAwait(true);

        _ = (await fixture.OpenAsync(fixture.SecondAsset, cancellation.Token).ConfigureAwait(true)).Should().BeFalse();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Participant.Prepared.Should().BeFalse();
    });

    [TestMethod]
    public Task OpenSceneAsync_NewerRequestRejectsOlderReadBeforeRetirement() => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(true);
        fixture.WriteScene("Third");
        var reading = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Scenes.BeforeRead = async path =>
        {
            if (string.Equals(path, fixture.PathOf("Second").FullPath, StringComparison.OrdinalIgnoreCase))
            {
                reading.SetResult();
                await release.Task.ConfigureAwait(true);
            }
        };
        var older = fixture.OpenAsync(fixture.SecondAsset, this.TestContext.CancellationToken);
        await reading.Task.ConfigureAwait(true);
        var newer = fixture.OpenAsync(fixture.Scenes.FileOf("Third").Asset, this.TestContext.CancellationToken);
        release.SetResult();

        _ = (await older.ConfigureAwait(true)).Should().BeFalse();
        _ = (await newer.ConfigureAwait(true)).Should().BeTrue();

        _ = fixture.Explorer.Scene!.AttachedObject.Name.Should().Be("Third");
        _ = fixture.Area.Documents.Should().ContainSingle()
            .Which.File.Path.Should().Be(fixture.PathOf("Third"));
        _ = fixture.Scenes.Open.Documents.Should().ContainSingle("the older request closed the document it read");
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task OpenSceneAsync_ApprovedReplacementRetiresThePreviousSceneAndAppliesClipboardPolicy(bool cut) => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(true);
        fixture.FirstDocument.History.AddChange("Old edit", static () => { });
        fixture.PromptDecision = CloseDecision.Discard;

        _ = (await fixture.OpenAsync(fixture.SecondAsset, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().BeTrue();

        var accepted = fixture.Explorer.Scene!.AttachedObject;
        _ = accepted.Name.Should().Be("Second");
        _ = ((SceneContent)fixture.FirstDocument.Content).Authoring.IsRetired.Should().BeTrue();
        _ = fixture.Scenes.Open.Documents.Should().ContainSingle().Which.File.Path.Should().Be(fixture.PathOf("Second"));
        _ = fixture.Explorer.ShownItems.OfType<SceneNodeAdapter>().Should().OnlyContain(adapter => ReferenceEquals(adapter.AttachedObject.Scene, accepted));
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(cut ? DroidNet.Controls.ClipboardState.Empty : DroidNet.Controls.ClipboardState.Copied);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().Be(!cut);
        _ = fixture.Explorer.ClipboardItems.Should().BeEmpty();
    });

    [TestMethod]
    public Task OpenSceneAsync_InvalidSourceAbandonsThePreparedCloseAndKeepsDirtySceneAndClipboard() => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(true);
        fixture.FirstDocument.History.AddChange("Edit", static () => { });
        fixture.Context.FileSystem.File.Delete(fixture.PathOf("Second").FullPath);

        _ = (await fixture.OpenAsync(fixture.SecondAsset, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().BeFalse();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.Participant.Prepared.Should().BeTrue("the unsaved edits prompt comes before the incoming scene is read");
        _ = fixture.Participant.Resumed.Should().BeTrue("a scene that does not open abandons the prepared close");
        _ = fixture.Participant.Closed.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().ContainSingle();
        _ = fixture.FirstDocument.IsDirty.Should().BeTrue();
    });

    [TestMethod]
    public Task OpenSceneAsync_CopySurvivesGuardedReplacementAndRepeatedPasteUsesSnapshotAndDestinationHistory() => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: false).ConfigureAwait(true);
        var source = fixture.First.RootNodes[0];
        source.Metadata.Name = "Changed after capture";
        source.Transform.LocalPosition = new System.Numerics.Vector3(100, 0, 0);

        _ = (await fixture.OpenAsync(fixture.SecondAsset, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().BeTrue();
        var accepted = fixture.Explorer.Scene!.AttachedObject;
        await fixture.Explorer.PasteItemsAsync(fixture.Explorer.Scene).ConfigureAwait(true);
        await fixture.Explorer.PasteItemsAsync(fixture.Explorer.Scene).ConfigureAwait(true);

        var pasted = accepted.RootNodes.Where(node => string.Equals(node.Metadata.Name, "Node", StringComparison.Ordinal)).ToArray();
        _ = pasted.Should().HaveCount(2);
        _ = pasted.Should().OnlyHaveUniqueItems(node => node.Id);
        _ = pasted.Should().NotContain(node => node.Id == source.Id);
        _ = pasted.Should().OnlyContain(node => node.Transform.LocalPosition == System.Numerics.Vector3.Zero);
        _ = pasted.Should().OnlyContain(node => ReferenceEquals(node.Scene, accepted));
        var destination = fixture.Scenes.Open.Find(fixture.PathOf("Second"))!;
        _ = destination.History.UndoStack.Should().HaveCount(2);
        _ = destination.IsDirty.Should().BeTrue();
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Copied);
    });

    [TestMethod]
    public Task OpenMaterialAsync_FocusChangeRetainsAcceptedSceneAndClipboard() => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(true);

        _ = (await fixture.OpenAsync(fixture.WriteMaterial("Test").Asset, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().BeTrue();

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
        _ = fixture.Participant.Prepared.Should().BeFalse();
        _ = fixture.Documents.GetOpenDocuments(fixture.Window).Should().HaveCount(2);
    });

    [TestMethod]
    public Task ProjectNotification_SameActivationSettingsRefreshKeepsExplicitSceneAndClipboard() => EnqueueAsync(async () =>
    {
        using var fixture = await LifetimeFixture.CreateAsync(cut: true).ConfigureAwait(true);

        fixture.Session.ChangeContext(fixture.Context with { DefaultSceneId = SceneId.New() });

        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.CurrentClipboardState.Should().Be(DroidNet.Controls.ClipboardState.Cut);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ProjectReplacement_CloseCancelled_KeepsTheSceneAndClipboard(bool cut) => EnqueueAsync(async () =>
    {
        // Arrange
        using var fixture = await LifetimeFixture.CreateAsync(cut).ConfigureAwait(true);
        fixture.Participant.PendingEdit = true;
        fixture.PromptDecision = CloseDecision.Cancel;

        // Act
        using var cancelled = await fixture.Documents.PrepareCloseAllWindowsAsync().ConfigureAwait(true);

        // Assert: the clipboard ends with the session, which a cancelled replacement keeps.
        _ = cancelled.Should().BeNull();
        _ = fixture.Explorer.Scene!.AttachedObject.Should().BeSameAs(fixture.First);
        _ = fixture.Explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
    });

    /// <summary>
    /// A workspace with the scene "First" open and one of its nodes selected and copied or cut, and the scene "Second"
    /// saved beside it.
    /// </summary>
    private sealed partial class LifetimeFixture : IDisposable
    {
        private LifetimeFixture()
        {
            var results = Mock.Of<IOperationResultPublisher>();
            var coordinator = new DocumentCloseCoordinator(this.CreateClosePrompt(), results);
            this.Documents = new DocumentTabs(coordinator);
            var selection = new SceneSelectionService();
            this.Scenes = new TestSceneDocuments(TestProject.Create(new MockFileSystem()), otherKinds: [new MaterialDocumentKind(new MaterialValidation())]);
            this.Session = new TestSession(this.Context);
            this.WriteScene("First", "Node");
            this.WriteScene("Second", "Destination");
            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync(It.IsAny<WindowId>())).Returns(Task.CompletedTask);
            _ = input.Setup(value => value.CommitAsync(It.IsAny<DocumentId>())).Returns(Task.CompletedTask);
            var files = new ContentPipeline.Relocation.ProjectDocumentFiles(this.Scenes.Session);
            this.Service = new DocumentService(
                this.Documents,
                coordinator,
                this.Scenes.Open,
                files,
                Mock.Of<IDocumentConfirmations>(),
                input.Object,
                new HostingContext
                {
                    Dispatcher = DispatcherQueue.GetForCurrentThread(),
                    Application = Application.Current,
                    DispatcherScheduler = new DispatcherQueueScheduler(DispatcherQueue.GetForCurrentThread()),
                });
            this.Area = new DocumentArea(
                this.Documents,
                this.Scenes.Open,
                new DocumentEditors(this.Scenes.Open, this.Scenes.Open.Kinds.Select(kind => new DocumentEditorDescriptor(kind, this.CreateEditor) { IsOnePerArea = kind is SceneDocumentKind })),
                this.Service,
                files,
                results,
                this.Window,
                static _ => { });
            this.Explorer = new SceneExplorerViewModel(
                Mock.Of<IRouter>(),
                this.Area,
                this.Scenes.Open,
                selection,
                session: this.Session);
            this.Participant = new SceneParticipant(this);
        }

        public WindowId Window { get; } = new(73);

        public TestSceneDocuments Scenes { get; }

        public ProjectContext Context => this.Scenes.Project.Context;

        public AssetPath SecondAsset => this.Scenes.FileOf("Second").Asset;

        public Document FirstDocument { get; private set; } = null!;

        public Scene First => ((SceneContent)this.FirstDocument.Content).Scene;

        public TestSession Session { get; }

        public DocumentTabs Documents { get; }

        public DocumentArea Area { get; }

        public DocumentService Service { get; }

        public SceneExplorerViewModel Explorer { get; }

        public SceneParticipant Participant { get; }

        public CloseDecision PromptDecision { get; set; } = CloseDecision.Discard;

        public static async Task<LifetimeFixture> CreateAsync(bool cut)
        {
            var fixture = new LifetimeFixture();
            try
            {
                _ = (await fixture.OpenAsync(fixture.Scenes.FileOf("First").Asset, fixture.Session.Lifetime).ConfigureAwait(true)).Should().BeTrue();
                fixture.FirstDocument = fixture.Scenes.Open.Find(fixture.PathOf("First"))!;
                var adapter = await fixture.Explorer.FindAdapterByNodeIdAsync(fixture.First.RootNodes[0].Id).ConfigureAwait(true);
                fixture.Explorer.SelectDisplayedItem(adapter!, [.. fixture.Explorer.ShownItems], isControlDown: false, isShiftDown: false);
                if (cut)
                {
                    await fixture.Explorer.CutItemsAsync([adapter!]).ConfigureAwait(true);
                }
                else
                {
                    await fixture.Explorer.CopyItemsAsync([adapter!]).ConfigureAwait(true);
                }

                return fixture;
            }
            catch
            {
                fixture.Dispose();
                throw;
            }
        }

        public async Task<bool> OpenAsync(AssetPath asset, CancellationToken cancellationToken = default)
            => (await this.Area.OpenAsync(asset, cancellationToken).ConfigureAwait(true)).Succeeded;

        public FileSystemPath PathOf(string name) => this.Scenes.FileOf(name).Path;

        /// <summary>Writes a scene file, its nodes complete with their default components.</summary>
        /// <param name="name">The scene's name, which names its file.</param>
        /// <param name="nodes">The names of the scene's root nodes.</param>
        public void WriteScene(string name, params string[] nodes)
        {
            var scene = new Scene { Name = name };
            foreach (var node in nodes)
            {
                scene.RootNodes.Add(new SceneNode(scene, node));
            }

            _ = this.Scenes.Write(scene.Dehydrate() with { Id = SceneId.New() });
        }

        /// <summary>Writes a new material file below the Content mount.</summary>
        /// <param name="name">The material's name.</param>
        /// <returns>The material's file.</returns>
        public DocumentFile WriteMaterial(string name)
            => new(
                this.Scenes.Project.WriteFile($"/Content/Materials/{name}.omat.json", MaterialTemplates.New(name)),
                AssetPath.Parse($"/Content/Materials/{name}.omat"));

        public void Dispose()
        {
            this.Explorer.Dispose();
            this.Area.Dispose();
            this.Service.Dispose();
            this.Session.Dispose();
            this.Scenes.Dispose();
        }

        // The first scene's editor is the participant the tests watch; the other documents need none.
        private object CreateEditor(DocumentTab tab)
            => tab.Document.File.Path == this.PathOf("First") ? this.Participant : new object();

        // The close prompt answers with the decision the test set: cancel, or save or discard every document it lists.
        private IDocumentClosePrompt CreateClosePrompt()
        {
            var prompt = new Mock<IDocumentClosePrompt>();
            _ = prompt.Setup(value => value.ConfirmAsync(It.IsAny<WindowId>(), It.IsAny<IReadOnlyList<DocumentCloseItem>>()))
                .Returns(async (WindowId window, IReadOnlyList<DocumentCloseItem> items) =>
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
                            _ = await item.SaveAsync().ConfigureAwait(true);
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
            => this.SaveSucceeds && (await fixture.FirstDocument.SaveAsync(fixture.Session.Lifetime).ConfigureAwait(true)).Succeeded;

        public Task CloseAsync(bool discard)
        {
            this.Closed = true;
            fixture.Scenes.Open.Close(fixture.FirstDocument);
            return Task.CompletedTask;
        }

        public void ResumeEditing() => this.Resumed = true;
    }
}
