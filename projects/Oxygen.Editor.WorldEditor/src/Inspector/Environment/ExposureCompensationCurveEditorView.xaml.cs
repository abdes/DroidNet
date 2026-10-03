// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns curve presentation and numeric adapters, not the parent-owned edit model.</summary>
[ViewModel(typeof(ExposureCompensationCurveEditorViewModel))]
public sealed partial class ExposureCompensationCurveEditorView
{
    /// <summary>Initializes a new instance of the <see cref="ExposureCompensationCurveEditorView"/> class.</summary>
    public ExposureCompensationCurveEditorView()
    {
        this.InitializeComponent();
    }

    private void OnEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.Begin(args);

    private void OnEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.Complete(args);

    private void OnKeyValidate(object? sender, ValidationEventArgs<float> args)
        => args.IsValid = sender is FrameworkElement { DataContext: ExposureCompensationKeyViewModel key, Tag: string coordinate }
            && this.ViewModel?.Validate(key, string.Equals(coordinate, "MeteredEv", StringComparison.Ordinal), args.NewValue) == true;

    private void OnRemoveKey(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ExposureCompensationKeyViewModel key })
        {
            this.ViewModel?.RemoveKeyCommand.Execute(key);
        }
    }
}
