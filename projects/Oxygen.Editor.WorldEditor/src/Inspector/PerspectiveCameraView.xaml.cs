// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Perspective camera inspector view.
/// </summary>
[ViewModel(typeof(PerspectiveCameraViewModel))]
public sealed partial class PerspectiveCameraView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PerspectiveCameraView"/> class.
    /// </summary>
    public PerspectiveCameraView()
    {
        this.InitializeComponent();
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(args);
}
