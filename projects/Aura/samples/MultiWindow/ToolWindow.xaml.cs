// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// A tool window for lightweight utilities.
/// </summary>
/// <remarks>
/// Theme synchronization is handled centrally by the WindowManagerService.
/// This window focuses solely on its tool-specific UI and behavior.
/// Uses a custom compact title bar without system chrome.
/// </remarks>
public sealed partial class ToolWindow : Window
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ToolWindow"/> class.
    /// </summary>
    public ToolWindow()
    {
        this.InitializeComponent();
        this.Title = "Tool Window";

        // Hide system title bar and use custom title bar
        // This must be set before the window is shown to prevent system caption buttons from appearing
        this.ExtendsContentIntoTitleBar = true;

        // Set a smaller default size for tool windows
        this.AppWindow.Resize(new Windows.Graphics.SizeInt32(400, 300));

        // Set the custom title bar as the drag region and hide system caption buttons
        // Must be called immediately after InitializeComponent to prevent system buttons from showing
        this.SetTitleBar(this.CustomTitleBar);
    }

    /// <summary>
    /// Handles the close button click event.
    /// </summary>
    private void CloseButton_Click(object sender, RoutedEventArgs e) => this.Close();
}
