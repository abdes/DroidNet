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
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>Bridges document Save/Close to the existing numeric control's text-commit behavior.</summary>
/// <param name="windows">The owner-window resolver.</param>
public sealed class DocumentInputCommitter(IWindowManagerService windows) : IDocumentInputCommitter
{
    /// <inheritdoc/>
    public Task CommitAsync(WindowId windowId)
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
