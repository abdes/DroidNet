// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using DroidNet.Mvvm;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>The document host makes each tab's editor from its kind's descriptor, and saves documents through their editors.</summary>
[TestClass]
public sealed partial class DocumentHostViewModelTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Each shown document gets the editor its kind's descriptor makes, and the selected one is active.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task HostCreatesEachEditorFromItsKindsDescriptor()
    {
        using var fixture = new HostFixture(new WindowId(81));
        var scene = await fixture.OpenSceneAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var material = await fixture.OpenMaterialAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = (await fixture.Area.ShowAsync(scene).ConfigureAwait(false)).Should().BeTrue();
        _ = (await fixture.Area.ShowAsync(material).ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Editors.Select(static editor => (editor.KindName, editor.Document)).Should().Equal(("Scene", scene), ("Material", material));
        _ = fixture.Host.ActiveEditor.Should().BeSameAs(fixture.Editors[1]);
        _ = fixture.Host.SaveActiveDocumentCommand.CanExecute(parameter: null).Should().BeTrue();
    }

    /// <summary>Saving by asset saves only the listed open documents, each through its editor.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task HostSavesTheListedAssetsThroughTheirEditors()
    {
        using var fixture = new HostFixture(new WindowId(82));
        var scene = await fixture.OpenSceneAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var material = await fixture.OpenMaterialAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await fixture.Area.ShowAsync(scene).ConfigureAwait(false);
        _ = await fixture.Area.ShowAsync(material).ConfigureAwait(false);
        scene.History.AddChange("Scene edit", static () => { });
        material.History.AddChange("Material edit", static () => { });

        var saved = await fixture.Host.SaveDocumentsAsync([material.File.Asset, AssetPath.Parse("/Content/Materials/Closed.omat")]).ConfigureAwait(false);

        _ = saved.Should().BeTrue();
        _ = material.IsDirty.Should().BeFalse();
        _ = scene.IsDirty.Should().BeTrue();
        _ = fixture.Editors.Single(editor => ReferenceEquals(editor.Document, material)).Saves.Should().Be(1);
        _ = fixture.Editors.Single(editor => ReferenceEquals(editor.Document, scene)).Saves.Should().Be(0);
    }

    /// <summary>A document area over real tabs and open documents, with a document host whose editors record their saves.</summary>
    private sealed partial class HostFixture : IDisposable
    {
        private readonly SceneDocumentFiles files = new();

        public HostFixture(WindowId window)
        {
            var close = new DocumentCloseCoordinator(Mock.Of<IDocumentClosePrompt>(), Mock.Of<IOperationResultPublisher>());
            var tabs = new EditorDocumentService(closeCoordinator: close);
            this.Area = new DocumentArea(
                tabs,
                this.files.Documents,
                this.files.Documents.Kinds.Select(kind => new DocumentEditorDescriptor(kind, tab =>
                {
                    var editor = new RecordingEditor(kind.Name, tab.Document);
                    this.Editors.Add(editor);
                    return editor;
                })),
                window,
                static _ => { });
            var engine = new Mock<IEngineService>();
            _ = engine.Setup(value => value.ReleaseOwnerSurfacesAsync(It.IsAny<SurfaceOwnerId>())).Returns(ValueTask.CompletedTask);
            this.Host = new DocumentHostViewModel(
                tabs,
                this.Area,
                this.files.Documents,
                Mock.Of<IViewLocator>(),
                engine.Object,
                close,
                Mock.Of<IWindowManagerService>(),
                window);
        }

        public DocumentArea Area { get; }

        public DocumentHostViewModel Host { get; }

        public List<RecordingEditor> Editors { get; } = [];

        public async Task<Document> OpenSceneAsync(CancellationToken cancellationToken)
        {
            this.files.Write("Main", SceneDocumentFiles.Data(SceneId.New(), "Main", "Node"));
            return (await this.files.OpenAsync("Main", cancellationToken).ConfigureAwait(false)).Document;
        }

        public async Task<Document> OpenMaterialAsync(CancellationToken cancellationToken)
        {
            var opened = await this.files.Documents.OpenAsync(this.files.WriteMaterial("Red"), cancellationToken).ConfigureAwait(false);
            return opened.Document ?? throw new InvalidOperationException(opened.Problem);
        }

        public void Dispose()
        {
            this.Host.Dispose();
            this.Area.Dispose();
            this.files.Dispose();
        }
    }

    /// <summary>Stands in for a kind's editor: it saves its document and counts the saves.</summary>
    /// <param name="kindName">The name of the kind whose descriptor made the editor.</param>
    /// <param name="document">The edited document.</param>
    private sealed class RecordingEditor(string kindName, Document document) : IDocumentCloseParticipant, IAsyncSaveable
    {
        public string KindName { get; } = kindName;

        public Document Document { get; } = document;

        public int Saves { get; private set; }

        public async Task SaveAsync()
        {
            this.Saves++;
            _ = await this.Document.SaveAsync().ConfigureAwait(false);
        }

        public Task PrepareForCloseAsync() => Task.CompletedTask;

        public async Task<bool> SaveForCloseAsync() => (await this.Document.SaveAsync().ConfigureAwait(false)).Succeeded;

        public Task CloseAsync(bool discard) => Task.CompletedTask;

        public void ResumeEditing()
        {
        }
    }
}
