// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// A demo page for the <see cref="ColorBox" /> control: linear, sRGB and high dynamic range colors, alpha, scale
/// factors without a picker, mixed values and the edit sessions a pick reports.
/// </summary>
[ViewModel(typeof(ColorBoxDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Views with injectable ViewModel must be public")]
public sealed partial class ColorBoxDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ColorBoxDemoView" /> class.
    /// </summary>
    public ColorBoxDemoView() => this.InitializeComponent();

    private static string Describe(ColorBoxEditSessionEventArgs args)
        => args.Channel is { } channel ? $"channel {ColorBox.ChannelName(channel)} ({args.InteractionKind})" : $"pick ({args.InteractionKind})";

    private void OnTintEditStarted(object? sender, ColorBoxEditSessionEventArgs args)
        => this.ViewModel?.Log($"Started {Describe(args)}");

    private void OnTintEditCompleted(object? sender, ColorBoxEditSessionEventArgs args)
        => this.ViewModel?.Log($"{args.CompletionKind} {Describe(args)}");

    private void OnTintPicked(object? sender, ColorBoxColorPickedEventArgs args)
    {
        this.ViewModel?.ApplyTint(args.Color);
        args.Handled = true;
    }
}
