// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
namespace Oxygen.Editor.Shell.Documents;

/// <summary>Prepares documents before an Aura window closes and commits only after every guard approves.</summary>
public sealed class WindowCloseCoordinator : IDisposable
{
    private readonly IWindowManagerService windows;
    private readonly IDocumentTabs documents;
    private readonly WindowId windowId;

    /// <summary>Initializes a new instance of the <see cref="WindowCloseCoordinator"/> class as the close guard for a workspace.</summary>
    /// <param name="windows">The window lifecycle service.</param>
    /// <param name="documents">The tabs of every window.</param>
    /// <param name="windowId">The workspace window.</param>
    public WindowCloseCoordinator(IWindowManagerService windows, IDocumentTabs documents, WindowId windowId)
    {
        this.windows = windows;
        this.documents = documents;
        this.windowId = windowId;
        windows.WindowClosing += this.OnWindowClosingAsync;
    }

    /// <inheritdoc/>
    public void Dispose() => this.windows.WindowClosing -= this.OnWindowClosingAsync;

    private async Task OnWindowClosingAsync(object? sender, WindowClosingEventArgs args)
    {
        if (args.WindowId != this.windowId || args.Cancel)
        {
            return;
        }

        var window = this.windows.GetWindow(this.windowId);
        if (window is null)
        {
            args.Cancel = true;
            return;
        }

        var transaction = await window.DispatcherQueue.EnqueueAsync(
            () => this.documents.PrepareCloseAllAsync(this.windowId)).ConfigureAwait(false);
        if (transaction is null)
        {
            args.Cancel = true;
            return;
        }

        args.AddCompletionTask(approved => window.DispatcherQueue.EnqueueAsync(async () =>
        {
            using (transaction)
            {
                if (approved)
                {
                    args.Cancel = !await transaction.CommitAsync().ConfigureAwait(true);
                }
            }
        }));
    }
}
