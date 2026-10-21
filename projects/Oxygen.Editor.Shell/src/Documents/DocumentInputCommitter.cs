// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using DroidNet.Controls;
using DroidNet.Documents;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// Commits the pending text of the focused <see cref="NumberBox"/> in a window, the way leaving the box would.
/// </summary>
/// <param name="windows">The window manager, which finds the window.</param>
/// <param name="tabs">The tabs of every window, which find the window that shows a document.</param>
public sealed class DocumentInputCommitter(IWindowManagerService windows, IDocumentTabs tabs) : IDocumentInputCommitter
{
    /// <inheritdoc/>
    public Task CommitAsync(DocumentId document)
        => tabs.FindWindow(document) is { } window ? this.CommitAsync(window) : Task.CompletedTask;

    /// <inheritdoc/>
    public Task CommitAsync(WindowId window)
    {
        var owner = windows.GetWindow(window) ?? throw new InvalidOperationException("The document window is unavailable.");
        return owner.DispatcherQueue.EnqueueAsync(() =>
        {
            if (owner.Window.Content?.XamlRoot is not { } root)
            {
                return;
            }

            var focused = FocusManager.GetFocusedElement(root) as DependencyObject;
            while (focused is not null)
            {
                if (focused is NumberBox number)
                {
                    number.CompletePendingTextEdit();
                    return;
                }

                focused = VisualTreeHelper.GetParent(focused);
            }
        });
    }
}
