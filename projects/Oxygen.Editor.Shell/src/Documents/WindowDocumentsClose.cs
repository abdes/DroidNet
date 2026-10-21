// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The documents' part of closing their window: asks about every unsaved document of the window first, and closes
///     the documents only once every participant of the close prepared it.
/// </summary>
public sealed class WindowDocumentsClose : IWindowCloseParticipant, IDisposable
{
    private readonly IWindowManagerService windows;
    private readonly IDocumentTabs documents;
    private readonly WindowId windowId;
    private readonly IDisposable membership;

    /// <summary>Initializes a new instance of the <see cref="WindowDocumentsClose"/> class, which takes part in its window's close.</summary>
    /// <param name="closes">The close of the editor's windows.</param>
    /// <param name="windows">The window lifecycle service.</param>
    /// <param name="documents">The tabs of every window.</param>
    /// <param name="windowId">The workspace window.</param>
    public WindowDocumentsClose(ICloseAndExit closes, IWindowManagerService windows, IDocumentTabs documents, WindowId windowId)
    {
        ArgumentNullException.ThrowIfNull(closes);
        this.windows = windows;
        this.documents = documents;
        this.windowId = windowId;
        this.membership = closes.Join(this);
    }

    /// <inheritdoc/>
    public WindowCloseLevel Level => WindowCloseLevel.Document;

    /// <inheritdoc/>
    public void Dispose() => this.membership.Dispose();

    /// <inheritdoc/>
    public async Task<WindowCloseStep?> PrepareAsync(WindowClose close)
    {
        ArgumentNullException.ThrowIfNull(close);
        if (close.Window != this.windowId)
        {
            return WindowCloseStep.None;
        }

        if (this.windows.GetWindow(this.windowId) is not { } window)
        {
            return null;
        }

        var transaction = await window.DispatcherQueue.EnqueueAsync(
            () => this.documents.PrepareCloseAllAsync(this.windowId)).ConfigureAwait(false);
        return transaction is null
            ? null
            : new WindowCloseStep(
                commit: () => window.DispatcherQueue.EnqueueAsync(transaction.CommitAsync),
                prepared: transaction);
    }
}
