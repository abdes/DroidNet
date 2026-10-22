// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Mvvm;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>
///     A window's document area shows the view of the active document's editor, and lets the leaving editor's view
///     release what it holds, such as its native surfaces, before the next view shows. It makes one view per editor,
///     kept while the editor's document has a tab in the window.
/// </summary>
[TestClass]
public sealed class DocumentAreaViewModelTests : IDisposable
{
    private readonly Notes notes = new();
    private readonly Mock<IDocumentArea> area = new();
    private readonly Mock<IDocumentService> documents = new();
    private readonly Mock<IViewLocator> views = new();
    private readonly List<Document> open = [];
    private readonly List<string> log = [];
    private readonly DocumentAreaViewModel sut;

    public DocumentAreaViewModelTests()
    {
        _ = this.area.SetupGet(value => value.Documents).Returns(this.open);
        this.sut = new DocumentAreaViewModel(this.documents.Object, this.area.Object, Mock.Of<IDocumentConfirmations>(), new WindowId(1), this.views.Object);
        this.sut.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(DocumentAreaViewModel.ActiveEditorView), StringComparison.Ordinal))
            {
                this.log.Add($"show {(this.sut.ActiveEditorView as View)?.Name ?? "nothing"}");
            }
        };
    }

    public TestContext TestContext { get; set; } = null!;

    [TestCleanup]
    public void Dispose()
    {
        this.sut.Dispose();
        this.notes.Dispose();
    }

    [TestMethod]
    public async Task TheLeavingEditorsViewDeactivatesBeforeTheNextShows()
    {
        var material = await this.OpenAsync("material").ConfigureAwait(true);
        var texture = await this.OpenAsync("texture").ConfigureAwait(true);
        this.Activate(material.Editor);

        this.Activate(texture.Editor);

        _ = this.log.Should().Equal("show material", "deactivate material", "show texture");
    }

    [TestMethod]
    public async Task NoActiveEditor_DeactivatesTheViewAndShowsNone()
    {
        this.Activate((await this.OpenAsync("material").ConfigureAwait(true)).Editor);

        this.Activate(editor: null);

        _ = this.log.Should().Equal("show material", "deactivate material", "show nothing");
        _ = this.sut.ActiveEditorView.Should().BeNull();
    }

    [TestMethod]
    public async Task ReturningToAnEditor_ShowsItsViewAgain()
    {
        var material = await this.OpenAsync("material").ConfigureAwait(true);
        var texture = await this.OpenAsync("texture").ConfigureAwait(true);
        this.Activate(material.Editor);
        this.Activate(texture.Editor);

        this.Activate(material.Editor);

        _ = this.sut.ActiveEditorView.Should().BeSameAs(material.View);
        _ = material.View.ViewModel.Should().BeSameAs(material.Editor);
        this.views.Verify(locator => locator.ResolveView(material.Editor), Times.Once());
    }

    [TestMethod]
    public async Task ClosingAnotherDocument_ReleasesItsEditorsView()
    {
        var material = await this.OpenAsync("material").ConfigureAwait(true);
        var texture = await this.OpenAsync("texture").ConfigureAwait(true);
        this.Activate(material.Editor);
        this.Activate(texture.Editor);

        this.Close(material.Document);

        _ = material.View.ViewModel.Should().BeNull();
        _ = texture.View.ViewModel.Should().BeSameAs(texture.Editor);
    }

    [TestMethod]
    public async Task ClosingTheShownDocument_ReleasesItsViewOnceItNoLongerShows()
    {
        var material = await this.OpenAsync("material").ConfigureAwait(true);
        this.Activate(material.Editor);

        this.Close(material.Document);
        var whileShown = material.View.ViewModel;
        this.Activate(editor: null);

        _ = whileShown.Should().BeSameAs(material.Editor);
        _ = material.View.ViewModel.Should().BeNull();
        _ = this.log.Should().Equal("show material", "deactivate material", "show nothing");
    }

    private async Task<Opened> OpenAsync(string name)
    {
        var document = await this.notes.OpenAsync(name, this.TestContext.CancellationToken).ConfigureAwait(true);
        var editor = new object();
        var view = new View(name, this.log);
        _ = this.documents.Setup(service => service.EditorOf(document)).Returns(editor);
        _ = this.views.Setup(locator => locator.ResolveView(editor)).Returns(view);
        this.open.Add(document);
        return new Opened(document, editor, view);
    }

    private void Activate(object? editor)
    {
        _ = this.area.SetupGet(value => value.ActiveEditor).Returns(editor);
        this.area.Raise(value => value.ActiveEditorChanged += null, EventArgs.Empty);
    }

    private void Close(Document document)
    {
        _ = this.open.Remove(document);
        this.area.Raise(value => value.DocumentsChanged += null, EventArgs.Empty);
    }

    private sealed record Opened(Document Document, object Editor, View View);

    private sealed class View(string name, List<string> log) : IDocumentEditorView, IViewFor
    {
        public string Name => name;

        public object? ViewModel { get; set; }

        public Task DeactivateAsync()
        {
            log.Add($"deactivate {name}");
            return Task.CompletedTask;
        }
    }
}
