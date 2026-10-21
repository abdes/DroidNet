// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Spot light inspector view.</summary>
[ViewModel(typeof(SpotLightViewModel))]
public sealed partial class SpotLightView
{
    /// <summary>Initializes a new instance of the <see cref="SpotLightView"/> class.</summary>
    public SpotLightView() => this.InitializeComponent();

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
