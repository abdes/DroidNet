// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Orthographic camera inspector view.
/// </summary>
[ViewModel(typeof(OrthographicCameraViewModel))]
public sealed partial class OrthographicCameraView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="OrthographicCameraView"/> class.
    /// </summary>
    public OrthographicCameraView()
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
