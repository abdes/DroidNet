// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Scene environment inspector view.
/// </summary>
[ViewModel(typeof(EnvironmentViewModel))]
public sealed partial class EnvironmentView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="EnvironmentView"/> class.
    /// </summary>
    public EnvironmentView()
    {
        this.InitializeComponent();
    }

    private void BackgroundPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        if (this.ViewModel is { } model && model.BackgroundColor != args.NewColor)
        {
            InspectorColorGestures.Apply(sender, owner => ((EnvironmentViewModel)owner).SetBackgroundColor(args.NewColor));
        }
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

    private void ColorPickerLoaded(object sender, RoutedEventArgs args)
    {
        if (sender is ColorPicker picker)
        {
            InspectorColorGestures.Attach(picker, this.ViewModel, "BackgroundColor");
        }
    }
}
