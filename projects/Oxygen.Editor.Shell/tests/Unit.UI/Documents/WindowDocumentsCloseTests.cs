// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Moq;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Unit.UI.Tests.Documents;

/// <summary>
/// A window's documents take part in its close: a document that is kept keeps the window open, and an approved close
/// closes the window's documents once the close commits.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class WindowDocumentsCloseTests : VisualUserInterfaceTests
{
    private static readonly WindowId Window = new(11);

    /// <summary>A document kept when asked, such as an unsaved one the user does not let go, keeps its window open.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AKeptDocumentKeepsTheWindowOpen() => EnqueueAsync(async () =>
    {
        var tabs = Tabs();
        var document = await OpenAsync(tabs).ConfigureAwait(true);
        tabs.DocumentClosing += (_, args) => args.AddVetoTask(Task.FromResult(false));
        using var participant = new WindowDocumentsClose(Closes(), Windows(), tabs, Window);

        var step = await participant.PrepareAsync(new WindowClose { Window = Window, IsLastWindow = false }).ConfigureAwait(true);

        _ = step.Should().BeNull();
        _ = tabs.GetOpenDocuments(Window).Should().Equal(document);
    });

    /// <summary>An approved close keeps the window's documents until it commits, then closes them.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AnApprovedCloseClosesTheDocumentsWhenItCommits() => EnqueueAsync(async () =>
    {
        var tabs = Tabs();
        _ = await OpenAsync(tabs).ConfigureAwait(true);
        using var participant = new WindowDocumentsClose(Closes(), Windows(), tabs, Window);

        using var step = await participant.PrepareAsync(new WindowClose { Window = Window, IsLastWindow = false }).ConfigureAwait(true);
        _ = tabs.GetOpenDocuments(Window).Should().ContainSingle("nothing closes before the close commits");
        _ = (await step!.CommitAsync().ConfigureAwait(true)).Should().BeTrue();

        _ = tabs.GetOpenDocuments(Window).Should().BeEmpty();
    });

    /// <summary>Another window's close leaves this window's documents alone.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AnotherWindowsCloseHasNothingToDo() => EnqueueAsync(async () =>
    {
        var tabs = Tabs();
        _ = await OpenAsync(tabs).ConfigureAwait(true);
        using var participant = new WindowDocumentsClose(Closes(), Windows(), tabs, Window);

        var step = await participant.PrepareAsync(new WindowClose { Window = new WindowId(12), IsLastWindow = false }).ConfigureAwait(true);

        _ = step.Should().BeSameAs(WindowCloseStep.None);
        _ = tabs.GetOpenDocuments(Window).Should().ContainSingle();
    });

    private static DocumentTabs Tabs()
        => new(new DocumentCloseCoordinator(Mock.Of<IDocumentClosePrompt>(), Mock.Of<IOperationResultPublisher>()));

    private static async Task<IDocumentMetadata> OpenAsync(DocumentTabs tabs)
    {
        var id = DocumentId.New();
        var document = Mock.Of<IDocumentMetadata>(metadata => metadata.DocumentId == id && metadata.Title == "Material");
        _ = await tabs.OpenDocumentAsync(Window, document).ConfigureAwait(true);
        return document;
    }

    private static ICloseAndExit Closes()
    {
        var closes = new Mock<ICloseAndExit>();
        _ = closes.Setup(value => value.Join(It.IsAny<IWindowCloseParticipant>())).Returns(Mock.Of<IDisposable>());
        return closes.Object;
    }

    private static IWindowManagerService Windows()
    {
        var window = new Mock<IManagedWindow>();
        _ = window.SetupGet(value => value.DispatcherQueue).Returns(DispatcherQueue.GetForCurrentThread());
        var windows = new Mock<IWindowManagerService>();
        _ = windows.Setup(value => value.GetWindow(Window)).Returns(window.Object);
        return windows.Object;
    }
}
