// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;
using Windows.UI.Core;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// A view for the World Editor workspace: a docking workspace view that also runs the editor-wide
/// scene shortcuts for keys its focused pane did not handle.
/// </summary>
public sealed partial class WorkspaceView : DockingWorkspaceView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="WorkspaceView"/> class.
    /// </summary>
    public WorkspaceView()
    {
        this.InitializeComponent();
        this.KeyDown += this.OnKeyDown;
    }

    private static bool IsKeyDown(VirtualKey key)
        => InputKeyboardSource.GetKeyStateForCurrentThread(key).HasFlag(CoreVirtualKeyStates.Down);

    // Text entry owns every key, including letters a text box lets bubble.
    private static bool IsTextEntry(DependencyObject? element)
    {
        for (var current = element; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (current is TextBox or RichEditBox or PasswordBox or AutoSuggestBox)
            {
                return true;
            }
        }

        return false;
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;
        if (e.Handled || e.KeyStatus.WasKeyDown || this.ViewModel is not WorkspaceViewModel workspace
            || (this.XamlRoot is { } root && IsTextEntry(FocusManager.GetFocusedElement(root) as DependencyObject)))
        {
            return;
        }

        if (SceneShortcuts.Map(e.Key, IsKeyDown(VirtualKey.Control), IsKeyDown(VirtualKey.Shift), IsKeyDown(VirtualKey.Menu)) is { } shortcut
            && workspace.RunSceneShortcut(shortcut))
        {
            e.Handled = true;
        }
    }
}
