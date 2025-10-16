// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// A tool window for lightweight utilities.
/// </summary>
public sealed partial class ToolWindow : Window
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ToolWindow"/> class.
    /// </summary>
    public ToolWindow()
    {
        this.InitializeComponent();
        this.Title = "Tool Window";

        // Set a smaller default size for tool windows
        this.AppWindow.Resize(new Windows.Graphics.SizeInt32(400, 300));
    }
}
