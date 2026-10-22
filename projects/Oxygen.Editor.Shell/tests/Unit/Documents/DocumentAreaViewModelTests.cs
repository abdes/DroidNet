// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Mvvm;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>
///     A window's document area shows the view of the active document's editor, and lets the leaving editor's view
///     release what it holds, such as its native surfaces, before the next view shows.
/// </summary>
[TestClass]
public sealed class DocumentAreaViewModelTests : IDisposable
{
    private readonly Mock<IDocumentArea> area = new();
    private readonly Mock<IViewLocator> views = new();
    private readonly List<string> log = [];
    private readonly DocumentAreaViewModel sut;

    public DocumentAreaViewModelTests()
    {
        this.sut = new DocumentAreaViewModel(Mock.Of<IDocumentService>(), this.area.Object, Mock.Of<IDocumentConfirmations>(), new WindowId(1), this.views.Object);
        this.sut.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(DocumentAreaViewModel.ActiveEditorView), StringComparison.Ordinal))
            {
                this.log.Add($"show {(this.sut.ActiveEditorView as View)?.Name ?? "nothing"}");
            }
        };
    }

    [TestCleanup]
    public void Dispose() => this.sut.Dispose();

    [TestMethod]
    public void TheLeavingEditorsViewDeactivatesBeforeTheNextShows()
    {
        var material = this.Editor("material");
        var texture = this.Editor("texture");
        this.Activate(material);

        this.Activate(texture);

        _ = this.log.Should().Equal("show material", "deactivate material", "show texture");
    }

    [TestMethod]
    public void NoActiveEditor_DeactivatesTheViewAndShowsNone()
    {
        this.Activate(this.Editor("material"));

        this.Activate(editor: null);

        _ = this.log.Should().Equal("show material", "deactivate material", "show nothing");
        _ = this.sut.ActiveEditorView.Should().BeNull();
    }

    private object Editor(string name)
    {
        var editor = new object();
        _ = this.views.Setup(locator => locator.ResolveView(editor)).Returns(new View(name, this.log));
        return editor;
    }

    private void Activate(object? editor)
    {
        _ = this.area.SetupGet(value => value.ActiveEditor).Returns(editor);
        this.area.Raise(value => value.ActiveEditorChanged += null, EventArgs.Empty);
    }

    private sealed class View(string name, List<string> log) : IDocumentEditorView
    {
        public string Name => name;

        public Task DeactivateAsync()
        {
            log.Add($"deactivate {name}");
            return Task.CompletedTask;
        }
    }
}
