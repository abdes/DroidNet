// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.DocumentWindows;

/// <summary>
///     The page of a document window: its document area, the active document's editor under the banners of its file's
///     state. The page is the window's frame: its keys run the window's commands, wherever the focus is in it.
/// </summary>
[ViewModel(typeof(DocumentWindowViewModel))]
public sealed partial class DocumentWindowView : UserControl
{
    private bool commandsBound;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentWindowView"/> class.
    /// </summary>
    public DocumentWindowView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) =>
        {
            if (!this.commandsBound && this.ViewModel?.Commands is { } commands)
            {
                CommandKeyBindings.Attach(this, commands);
                this.commandsBound = true;
            }
        };
    }
}
