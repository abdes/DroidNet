// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell.Unit.Tests.Windows;

/// <summary>
/// Closing a window is one negotiation (shell design section 6): every participant prepares it in level order, documents
/// first; then every step commits; then every step finalizes, before the window is destroyed. A participant that refuses
/// to prepare or to commit keeps the window open.
/// </summary>
[TestClass]
public sealed class CloseAndExitTests
{
    private static readonly WindowId Closing = new(1);

    /// <summary>Every participant prepares, then commits, then finalizes, each in level order.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CloseRunsEveryPhaseInLevelOrder()
    {
        var log = new List<string>();
        var windows = OpenWindows(Closing);
        using var service = new CloseAndExit(
            windows.Object,
            [new Participant(WindowCloseLevel.Application, log), new Participant(WindowCloseLevel.Session, log)]);
        using var documents = service.Join(new Participant(WindowCloseLevel.Document, log));

        var args = await CloseAsync(windows).ConfigureAwait(false);

        _ = args.Cancel.Should().BeFalse();
        _ = log.Should().Equal(
            "prepare Document", "prepare Session", "prepare Application",
            "commit Document", "commit Session", "commit Application",
            "finalize Document", "finalize Session", "finalize Application",
            "release Document", "release Session", "release Application");
    }

    /// <summary>A participant that refuses to prepare keeps the window open and releases the steps already prepared.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RefusedPreparationKeepsTheWindowOpen()
    {
        var log = new List<string>();
        var windows = OpenWindows(Closing);
        using var service = new CloseAndExit(
            windows.Object,
            [new Participant(WindowCloseLevel.Document, log), new Participant(WindowCloseLevel.Session, log, refusesToPrepare: true), new Participant(WindowCloseLevel.Application, log)]);

        var args = await CloseAsync(windows).ConfigureAwait(false);

        _ = args.Cancel.Should().BeTrue();
        _ = log.Should().Equal("prepare Document", "prepare Session", "release Document");
    }

    /// <summary>A step that refuses to commit keeps the window open: no step finalizes, and every step is released.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RefusedCommitKeepsTheWindowOpenWithoutFinalizing()
    {
        var log = new List<string>();
        var windows = OpenWindows(Closing);
        using var service = new CloseAndExit(
            windows.Object,
            [new Participant(WindowCloseLevel.Document, log, refusesToCommit: true), new Participant(WindowCloseLevel.Application, log)]);

        var args = await CloseAsync(windows).ConfigureAwait(false);

        _ = args.Cancel.Should().BeTrue();
        _ = log.Should().Equal("prepare Document", "prepare Application", "commit Document", "release Document", "release Application");
    }

    /// <summary>A participant that left takes no part in later closes.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ParticipantThatLeftTakesNoPart()
    {
        var log = new List<string>();
        var windows = OpenWindows(Closing);
        using var service = new CloseAndExit(windows.Object, []);
        service.Join(new Participant(WindowCloseLevel.Document, log)).Dispose();

        var args = await CloseAsync(windows).ConfigureAwait(false);

        _ = args.Cancel.Should().BeFalse();
        _ = log.Should().BeEmpty();
    }

    /// <summary>The close of the only open window is the last window's; with another window open it is not.</summary>
    /// <param name="others">How many other windows are open.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(0)]
    [DataRow(1)]
    public async Task CloseKnowsWhetherItIsTheLastWindow(int others)
    {
        WindowClose? seen = null;
        var participant = new Mock<IWindowCloseParticipant>();
        _ = participant.Setup(value => value.PrepareAsync(It.IsAny<WindowClose>()))
            .Callback<WindowClose>(close => seen = close)
            .ReturnsAsync(WindowCloseStep.None);
        var windows = OpenWindows([Closing, .. Enumerable.Range(2, others).Select(static id => new WindowId((ulong)id))]);
        using var service = new CloseAndExit(windows.Object, [participant.Object]);

        _ = await CloseAsync(windows).ConfigureAwait(false);

        _ = seen.Should().Be(new WindowClose { Window = Closing, IsLastWindow = others == 0 });
    }

    private static Mock<IWindowManagerService> OpenWindows(params WindowId[] open)
    {
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.OpenWindows)
            .Returns([.. open.Select(static id => Mock.Of<IManagedWindow>(window => window.Id == id))]);
        return windows;
    }

    // Raises the window's closing as Aura does: every handler, then the commits and finalizers of an approved close.
    private static async Task<WindowClosingEventArgs> CloseAsync(Mock<IWindowManagerService> windows)
    {
        var args = new WindowClosingEventArgs { WindowId = Closing };
        await windows.RaiseAsync(value => value.WindowClosing += null, windows.Object, args).ConfigureAwait(false);
        await args.CompleteAsync(!args.Cancel).ConfigureAwait(false);
        return args;
    }

    private sealed class Participant(WindowCloseLevel level, List<string> log, bool refusesToPrepare = false, bool refusesToCommit = false)
        : IWindowCloseParticipant
    {
        public WindowCloseLevel Level => level;

        public Task<WindowCloseStep?> PrepareAsync(WindowClose close)
        {
            log.Add($"prepare {level}");
            return Task.FromResult<WindowCloseStep?>(refusesToPrepare
                ? null
                : new WindowCloseStep(
                    commit: () =>
                    {
                        log.Add($"commit {level}");
                        return Task.FromResult(!refusesToCommit);
                    },
                    finalize: () =>
                    {
                        log.Add($"finalize {level}");
                        return Task.CompletedTask;
                    },
                    prepared: new Release(() => log.Add($"release {level}"))));
        }
    }

    private sealed class Release(Action release) : IDisposable
    {
        public void Dispose() => release();
    }
}
