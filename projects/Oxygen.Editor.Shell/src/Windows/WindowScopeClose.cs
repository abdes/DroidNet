// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using Microsoft.UI;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>
///     The window's scope's part of closing its window: once the close is certain, the scope is disposed, awaited, on the
///     window's UI thread.
/// </summary>
/// <remarks>
///     Every window scope ends with its window. The scope is a child of the session's, which disposes what is left when
///     the session closes.
/// </remarks>
public sealed class WindowScopeClose : IWindowCloseParticipant, IDisposable
{
    private readonly IWindowManagerService windows;
    private readonly ScopeHandle scope;
    private readonly WindowId windowId;
    private readonly IDisposable membership;

    /// <summary>Initializes a new instance of the <see cref="WindowScopeClose"/> class, which takes part in its window's close.</summary>
    /// <param name="closes">The close of the editor's windows.</param>
    /// <param name="windows">The window lifecycle service.</param>
    /// <param name="scope">The window's scope.</param>
    /// <param name="windowId">The window.</param>
    public WindowScopeClose(ICloseAndExit closes, IWindowManagerService windows, ScopeHandle scope, WindowId windowId)
    {
        ArgumentNullException.ThrowIfNull(closes);
        this.windows = windows;
        this.scope = scope;
        this.windowId = windowId;
        this.membership = closes.Join(this);
    }

    /// <inheritdoc/>
    public WindowCloseLevel Level => WindowCloseLevel.Window;

    /// <inheritdoc/>
    public void Dispose() => this.membership.Dispose();

    /// <inheritdoc/>
    public Task<WindowCloseStep?> PrepareAsync(WindowClose close)
    {
        ArgumentNullException.ThrowIfNull(close);
        if (close.Window != this.windowId || this.windows.GetWindow(this.windowId) is not { } window)
        {
            return Task.FromResult<WindowCloseStep?>(WindowCloseStep.None);
        }

        return Task.FromResult<WindowCloseStep?>(new WindowCloseStep(
            finalize: () => window.DispatcherQueue.EnqueueAsync(() => this.scope.DisposeAsync().AsTask())));
    }
}
