// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// A document window for content editing.
/// </summary>
/// <remarks>
/// Theme synchronization is handled centrally by the WindowManagerService.
/// This window focuses solely on its document editing UI and behavior.
/// </remarks>
public sealed partial class DocumentWindow : Window
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DocumentWindow"/> class.
    /// </summary>
    public DocumentWindow()
    {
        this.InitializeComponent();
        this.Title = "Document Window";

        // Set a comfortable size for document editing
        this.AppWindow.Resize(new Windows.Graphics.SizeInt32(800, 600));
    }
}
