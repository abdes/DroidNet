// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using Microsoft.UI;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>Reuses inline conflict actions for an ordinary Save outside the close workflow.</summary>
/// <param name="dialogs">The serialized dialog service.</param>
/// <param name="windows">The owner-window resolver.</param>
public sealed class DocumentConflictPrompt(IDialogService dialogs, IWindowManagerService windows) : IDocumentConflictPrompt
{
    private readonly ConditionalWeakTable<IDocumentConflictParticipant, PendingPrompt> pending = [];

    /// <inheritdoc/>
    public Task ShowAsync(WindowId windowId, IDocumentMetadata metadata, IDocumentConflictParticipant participant)
    {
        var prompt = this.pending.GetValue(participant, _ => new());
        lock (prompt)
        {
            return prompt.Task is { IsCompleted: false } current
                ? current : prompt.Task = this.ShowCoreAsync(windowId, metadata, participant);
        }
    }

    private Task ShowCoreAsync(WindowId windowId, IDocumentMetadata metadata, IDocumentConflictParticipant participant)
    {
        var window = windows.GetWindow(windowId) ?? throw new InvalidOperationException("The document window is unavailable.");
        return window.DispatcherQueue.EnqueueAsync(async () =>
        {
            var item = new DocumentCloseItem(metadata, () => Task.FromResult(false), participant);
            var panel = new DocumentConflictPanel(item, () => { });
            try
            {
                _ = await dialogs.ShowAsync(new DialogSpec($"Save conflict — {metadata.Title}", panel) { CloseButtonText = "Keep open", DefaultButton = DialogButton.Close }, windowId).ConfigureAwait(true);
            }
            finally
            {
                await panel.Pending.ConfigureAwait(true);
                await item.Pending.ConfigureAwait(true);
            }
        });
    }

    private sealed class PendingPrompt
    {
        public Task? Task { get; set; }
    }
}
