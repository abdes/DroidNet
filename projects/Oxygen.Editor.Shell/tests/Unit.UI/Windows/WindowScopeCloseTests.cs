// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DroidNet.Tests;
using DryIoc;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Moq;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell.Unit.UI.Tests.Windows;

/// <summary>A window that closes without its session disposes its own scope, once its close is certain.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class WindowScopeCloseTests : VisualUserInterfaceTests
{
    private static readonly WindowId Window = new(7);

    /// <summary>The window's scope outlives the window's prepared close, and ends when the close finalizes.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task TheScopeIsDisposedWhenTheWindowsCloseFinalizes() => EnqueueAsync(async () =>
    {
        using var host = new Container();
        await using var root = ScopeFactory.CreateRoot(host);
        var scope = new ScopeFactory().Create<IWindowLayer>(root, []);
        using var participant = new WindowScopeClose(Closes(), Windows(), scope, Window);

        using var step = await participant.PrepareAsync(new WindowClose { Window = Window, IsLastWindow = false }).ConfigureAwait(true);
        _ = scope.IsDisposed.Should().BeFalse("the close may still be refused");
        await step!.FinalizeAsync().ConfigureAwait(true);

        _ = scope.IsDisposed.Should().BeTrue();
    });

    /// <summary>Another window's close leaves the scope alone.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task AnotherWindowsCloseHasNothingToDo() => EnqueueAsync(async () =>
    {
        using var host = new Container();
        await using var root = ScopeFactory.CreateRoot(host);
        var scope = new ScopeFactory().Create<IWindowLayer>(root, []);
        using var participant = new WindowScopeClose(Closes(), Windows(), scope, Window);

        var step = await participant.PrepareAsync(new WindowClose { Window = new WindowId(8), IsLastWindow = false }).ConfigureAwait(true);

        _ = step.Should().BeSameAs(WindowCloseStep.None);
        _ = scope.IsDisposed.Should().BeFalse();
    });

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
