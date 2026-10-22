// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Documents;
using Microsoft.UI;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>
///     A tab that a drag moves between windows: detached from its window with its document open, attached in another,
///     and the window it leaves empty reported once another window has the tab.
/// </summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public sealed class DocumentTabsTransferTests
{
    private readonly WindowId workspace = new(1);
    private readonly WindowId documentWindow = new(2);
    private readonly DocumentTabs tabs = Tabs.New();
    private readonly List<string> events = [];
    private readonly List<WindowId> emptied = [];

    public DocumentTabsTransferTests()
    {
        this.tabs.DocumentAttached += (_, args) => this.events.Add($"attached {args.WindowId.Value}");
        this.tabs.DocumentActivated += (_, args) => this.events.Add($"activated {args.WindowId.Value}");
        this.tabs.DocumentDetached += (_, args) => this.events.Add($"detached {args.WindowId.Value}");
        this.tabs.WindowEmptied += (_, args) => this.emptied.Add(args.Window);
    }

    [TestMethod]
    public async Task DetachDocumentAsync_RemovesTheTabAndReturnsItsMetadata()
    {
        var material = await this.OpenAsync(this.documentWindow).ConfigureAwait(false);

        var detached = await this.tabs.DetachDocumentAsync(this.documentWindow, material.DocumentId).ConfigureAwait(false);

        _ = detached.Should().BeSameAs(material);
        _ = this.tabs.GetOpenDocuments(this.documentWindow).Should().BeEmpty();
        _ = this.tabs.GetActiveDocumentId(this.documentWindow).Should().BeNull();
        _ = this.tabs.FindWindow(material.DocumentId).Should().BeNull();
        _ = this.events.Should().EndWith($"detached {this.documentWindow.Value}");
    }

    /// <summary>The scene's tab is pinned for good: it never leaves the workspace window.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task DetachDocumentAsync_OfAPinnedTab_IsRefused()
    {
        var scene = new TestDocumentMetadata { Title = "Scene", IsPinnedHint = true };
        _ = await this.tabs.OpenDocumentAsync(this.workspace, scene).ConfigureAwait(false);

        var detached = await this.tabs.DetachDocumentAsync(this.workspace, scene.DocumentId).ConfigureAwait(false);

        _ = detached.Should().BeNull();
        _ = this.tabs.FindWindow(scene.DocumentId).Should().Be(this.workspace);
        _ = this.events.Should().NotContain(@event => @event.StartsWith("detached", StringComparison.Ordinal));
    }

    [TestMethod]
    public async Task DetachDocumentAsync_OfATabTheWindowDoesNotHave_IsRefused()
    {
        var material = await this.OpenAsync(this.workspace).ConfigureAwait(false);

        var detached = await this.tabs.DetachDocumentAsync(this.documentWindow, material.DocumentId).ConfigureAwait(false);

        _ = detached.Should().BeNull();
        _ = this.tabs.FindWindow(material.DocumentId).Should().Be(this.workspace);
    }

    /// <summary>The window has the tab before the tab is selected there, so its strip can select it.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task AttachDocumentAsync_AttachesTheTabBeforeSelectingIt()
    {
        var material = await this.OpenAsync(this.workspace).ConfigureAwait(false);
        _ = await this.tabs.DetachDocumentAsync(this.workspace, material.DocumentId).ConfigureAwait(false);
        this.events.Clear();

        var attached = await this.tabs.AttachDocumentAsync(this.documentWindow, material, shouldSelect: true).ConfigureAwait(false);

        _ = attached.Should().BeTrue();
        _ = this.events.Should().Equal($"attached {this.documentWindow.Value}", $"activated {this.documentWindow.Value}");
        _ = this.tabs.GetActiveDocumentId(this.documentWindow).Should().Be(material.DocumentId);
    }

    [TestMethod]
    public async Task MovingTheLastTab_ToAnotherWindow_ReportsItsWindowEmptied()
    {
        var material = await this.OpenAsync(this.documentWindow).ConfigureAwait(false);
        _ = await this.tabs.DetachDocumentAsync(this.documentWindow, material.DocumentId).ConfigureAwait(false);
        _ = this.emptied.Should().BeEmpty("the drag may still bring the tab back");

        _ = await this.tabs.AttachDocumentAsync(this.workspace, material).ConfigureAwait(false);

        _ = this.emptied.Should().Equal(this.documentWindow);
    }

    [TestMethod]
    public async Task MovingATab_BackToItsWindow_ReportsNothing()
    {
        var material = await this.OpenAsync(this.documentWindow).ConfigureAwait(false);
        _ = await this.tabs.DetachDocumentAsync(this.documentWindow, material.DocumentId).ConfigureAwait(false);

        _ = await this.tabs.AttachDocumentAsync(this.documentWindow, material).ConfigureAwait(false);

        _ = this.emptied.Should().BeEmpty();
        _ = this.tabs.FindWindow(material.DocumentId).Should().Be(this.documentWindow);
    }

    [TestMethod]
    public async Task MovingATab_OutOfAWindowWithOtherTabs_ReportsNothing()
    {
        var material = await this.OpenAsync(this.documentWindow).ConfigureAwait(false);
        _ = await this.OpenAsync(this.documentWindow).ConfigureAwait(false);
        _ = await this.tabs.DetachDocumentAsync(this.documentWindow, material.DocumentId).ConfigureAwait(false);

        _ = await this.tabs.AttachDocumentAsync(this.workspace, material).ConfigureAwait(false);

        _ = this.emptied.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ClosingTheLastTab_ReportsItsWindowEmptied()
    {
        var material = await this.OpenAsync(this.documentWindow).ConfigureAwait(false);

        _ = await this.tabs.CloseDocumentAsync(this.documentWindow, material.DocumentId).ConfigureAwait(false);

        _ = this.emptied.Should().Equal(this.documentWindow);
    }

    /// <summary>A window that closes, or whose session closes, closes all its tabs without being emptied by the user.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task ClosingAllTabs_ReportsNothing()
    {
        _ = await this.OpenAsync(this.documentWindow).ConfigureAwait(false);
        using var transaction = await this.tabs.PrepareCloseAllAsync(this.documentWindow).ConfigureAwait(false);

        _ = await transaction!.CommitAsync().ConfigureAwait(false);

        _ = this.tabs.GetOpenDocuments(this.documentWindow).Should().BeEmpty();
        _ = this.emptied.Should().BeEmpty();
    }

    private async Task<TestDocumentMetadata> OpenAsync(WindowId window)
    {
        var metadata = new TestDocumentMetadata { Title = "Material" };
        _ = await this.tabs.OpenDocumentAsync(window, metadata).ConfigureAwait(false);
        return metadata;
    }
}
