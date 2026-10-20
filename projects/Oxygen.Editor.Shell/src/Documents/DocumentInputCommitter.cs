// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using DroidNet.Controls;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// Commits the pending text of the focused <see cref="NumberBox"/> in one window, the way leaving the box would.
/// </summary>
/// <param name="windows">The window manager, which finds the window.</param>
/// <param name="windowId">The window whose focused control is committed.</param>
public sealed class DocumentInputCommitter(IWindowManagerService windows, WindowId windowId) : IDocumentInputCommitter
{
    /// <inheritdoc/>
    public Task CommitAsync()
    {
        var window = windows.GetWindow(windowId) ?? throw new InvalidOperationException("The document window is unavailable.");
        return window.DispatcherQueue.EnqueueAsync(() =>
        {
            if (window.Window.Content?.XamlRoot is not { } root)
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
