// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using DroidNet.Aura;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Composition.UI.Tests.TestSupport;

/// <summary>A window Aura has open: its id, its kind and, for a window of a project session, its session.</summary>
/// <param name="Id">The window.</param>
/// <param name="Category">Its kind.</param>
/// <param name="Session">The session it belongs to, if any.</param>
internal sealed record OpenWindow(WindowId Id, WindowCategory Category, IProjectSession? Session);

/// <summary>
/// Aura's window manager as the editor's close and session code read it: the open windows, each with its category and
/// session metadata. Closing a window removes it, as the window manager does once the window is destroyed.
/// </summary>
internal static class EditorWindowsFake
{
    /// <summary>
    /// Shows these windows, and only these, in the editor scope's window manager. Services that resolved the window
    /// manager before see them too: the scopes have one fake window manager, which this reconfigures.
    /// </summary>
    /// <param name="scopes">The editor's scopes.</param>
    /// <param name="open">The open windows.</param>
    /// <returns>The window manager, whose closes a test can verify from now on.</returns>
    public static Mock<IWindowManagerService> ShowWindows(this EditorScopes scopes, params OpenWindow[] open)
    {
        ArgumentNullException.ThrowIfNull(scopes);
        var windows = open.Select(static window => Managed(window)).ToList();
        var manager = scopes.Windows;
        manager.Reset();
        _ = manager.SetupGet(value => value.OpenWindows).Returns(() => [.. windows]);
        _ = manager.SetupGet(value => value.WindowEvents).Returns(Observable.Never<WindowLifecycleEvent>());
        _ = manager.Setup(value => value.CloseWindowAsync(It.IsAny<WindowId>()))
            .Returns<WindowId>(id => Task.FromResult(windows.RemoveAll(window => window.Id == id) > 0));
        return manager;
    }

    private static IManagedWindow Managed(OpenWindow window)
    {
        var managed = new Mock<IManagedWindow>();
        _ = managed.SetupGet(value => value.Id).Returns(window.Id);
        _ = managed.SetupGet(value => value.Category).Returns(window.Category);
        _ = managed.SetupGet(value => value.Metadata).Returns(window.Session is null
            ? null
            : new Dictionary<string, object>(StringComparer.Ordinal) { [EditorWindows.SessionMetadataKey] = window.Session });
        return managed.Object;
    }
}
