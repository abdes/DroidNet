// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using DroidNet.Controls.OutputLog;
using DroidNet.Controls.OutputLog.Output;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml;

/// <summary>
/// A custom control to display the output logs in a <see cref="RichTextBlock" /> control.
/// </summary>
public sealed partial class OutputLogView
{
    public static readonly DependencyProperty OutputLogSinkProperty = DependencyProperty.Register(
        nameof(OutputLogSink),
        typeof(DelegatingSink<RichTextBlockSink>),
        typeof(OutputLogView),
        new PropertyMetadata(default(DelegatingSink<RichTextBlockSink>)));

    public OutputLogView() => this.InitializeComponent();

    public DelegatingSink<RichTextBlockSink> OutputLogSink
    {
        get => (DelegatingSink<RichTextBlockSink>)this.GetValue(OutputLogSinkProperty);
        set => this.SetValue(OutputLogSinkProperty, value);
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        var appliedTheme = Themes.Builtin[this.OutputLogSink.Theme ?? Themes.Literate];

        var renderer = new OutputTemplateRenderer(
            appliedTheme,
            this.OutputLogSink.OutputTemplate,
            this.OutputLogSink.FormatProvider);
        this.OutputLogSink.DelegateSink = new RichTextBlockSink(this.RichTextBlock, renderer);
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.OutputLogSink.DelegateSink = null;
    }
}
