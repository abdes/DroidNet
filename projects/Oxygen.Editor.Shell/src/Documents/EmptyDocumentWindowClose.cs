// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     Closes a document window once its last tab has left it: the tab was closed, or a drag moved it to another window.
///     A document window exists only to show its tabs.
/// </summary>
/// <remarks>
///     A drag that only passes over another window and comes back leaves the window open: the window is empty only once
///     another window attached the tab.
/// </remarks>
public sealed class EmptyDocumentWindowClose : IDisposable
{
    private readonly IDocumentTabs tabs;
    private readonly IWindowManagerService windows;
    private readonly WindowId windowId;

    /// <summary>Initializes a new instance of the <see cref="EmptyDocumentWindowClose"/> class, which follows its window's tabs.</summary>
    /// <param name="tabs">The tabs of every window.</param>
    /// <param name="windows">The window lifecycle service, which closes the window.</param>
    /// <param name="windowId">The document window.</param>
    public EmptyDocumentWindowClose(IDocumentTabs tabs, IWindowManagerService windows, WindowId windowId)
    {
        ArgumentNullException.ThrowIfNull(tabs);
        ArgumentNullException.ThrowIfNull(windows);
        this.tabs = tabs;
        this.windows = windows;
        this.windowId = windowId;
        tabs.WindowEmptied += this.OnWindowEmptied;
    }

    /// <inheritdoc/>
    public void Dispose() => this.tabs.WindowEmptied -= this.OnWindowEmptied;

    private async void OnWindowEmptied(object? sender, WindowEmptiedEventArgs args)
    {
        if (args.Window == this.windowId)
        {
            _ = await this.windows.CloseWindowAsync(this.windowId).ConfigureAwait(true);
        }
    }
}
