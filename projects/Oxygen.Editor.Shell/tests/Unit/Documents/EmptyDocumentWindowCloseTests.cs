// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>A document window closes once its last tab has left it, and only then.</summary>
[TestClass]
public sealed class EmptyDocumentWindowCloseTests
{
    private static readonly WindowId Workspace = new(1);
    private static readonly WindowId DocumentWindow = new(2);

    private readonly DocumentTabs tabs = Tabs.New();
    private readonly Mock<IWindowManagerService> windows = new();

    public EmptyDocumentWindowCloseTests()
        => _ = this.windows.Setup(value => value.CloseWindowAsync(It.IsAny<WindowId>())).ReturnsAsync(value: true);

    /// <summary>The window closes when a drag moved its last tab to another window.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task TheWindowClosesWhenItsLastTabMovedAway()
    {
        using var close = new EmptyDocumentWindowClose(this.tabs, this.windows.Object, DocumentWindow);
        var material = new TestDocumentMetadata { Title = "Material" };
        _ = await this.tabs.OpenDocumentAsync(DocumentWindow, material).ConfigureAwait(false);

        var tab = await this.tabs.DetachDocumentAsync(DocumentWindow, material.DocumentId).ConfigureAwait(false);
        this.windows.Verify(value => value.CloseWindowAsync(It.IsAny<WindowId>()), Times.Never(), "the drag may bring the tab back");
        _ = await this.tabs.AttachDocumentAsync(Workspace, tab!).ConfigureAwait(false);

        this.windows.Verify(value => value.CloseWindowAsync(DocumentWindow), Times.Once());
    }

    /// <summary>The window closes when its last tab is closed.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task TheWindowClosesWhenItsLastTabCloses()
    {
        using var close = new EmptyDocumentWindowClose(this.tabs, this.windows.Object, DocumentWindow);
        var material = new TestDocumentMetadata { Title = "Material" };
        _ = await this.tabs.OpenDocumentAsync(DocumentWindow, material).ConfigureAwait(false);

        _ = await this.tabs.CloseDocumentAsync(DocumentWindow, material.DocumentId).ConfigureAwait(false);

        this.windows.Verify(value => value.CloseWindowAsync(DocumentWindow), Times.Once());
    }

    /// <summary>Another window that empties does not close this one.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task AnotherWindowEmptiedLeavesThisOneOpen()
    {
        using var close = new EmptyDocumentWindowClose(this.tabs, this.windows.Object, DocumentWindow);
        var material = new TestDocumentMetadata { Title = "Material" };
        _ = await this.tabs.OpenDocumentAsync(Workspace, material).ConfigureAwait(false);

        _ = await this.tabs.CloseDocumentAsync(Workspace, material.DocumentId).ConfigureAwait(false);

        this.windows.Verify(value => value.CloseWindowAsync(It.IsAny<WindowId>()), Times.Never());
    }
}
