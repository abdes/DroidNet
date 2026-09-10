// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>Prepares documents before an Aura window closes and commits only after every guard approves.</summary>
public sealed partial class WorkspaceCloseCoordinator : IDisposable
{
    private readonly IWindowManagerService windows;
    private readonly IEditorDocumentService documents;
    private readonly WindowId windowId;

    /// <summary>Initializes a new instance of the <see cref="WorkspaceCloseCoordinator"/> class as the close guard for a workspace.</summary>
    /// <param name="windows">The window lifecycle service.</param>
    /// <param name="documents">The editor document service.</param>
    /// <param name="windowId">The workspace window.</param>
    public WorkspaceCloseCoordinator(IWindowManagerService windows, IEditorDocumentService documents, WindowId windowId)
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
