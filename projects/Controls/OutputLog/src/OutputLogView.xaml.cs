// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog;
using DroidNet.Controls.OutputLog.Output;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
/// A custom control to display the output logs in a <see cref="RichTextBlock"/> control.
/// </summary>
/// <remarks>
/// <para>
/// This control integrates with Serilog for logging functionality and allows you to output logs to a <see cref="RichTextBlock"/> control with theming support.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this control to display log events in a rich text format. Configure the <see cref="OutputLogSink"/> property to specify the sink that will handle the log events.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// <droidnet:OutputLogView x:Name="outputLogView" />
/// ]]></code>
/// </para>
/// </remarks>
public sealed partial class OutputLogView
{
    /// <summary>
    /// Identifies the <see cref="OutputLogSink"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty OutputLogSinkProperty = DependencyProperty.Register(
        nameof(OutputLogSink),
        typeof(DelegatingSink<RichTextBlockSink>),
        typeof(OutputLogView),
        new PropertyMetadata(default(DelegatingSink<RichTextBlockSink>)));

    /// <summary>
    /// Initializes a new instance of the <see cref="OutputLogView"/> class.
    /// </summary>
    public OutputLogView()
    {
        this.InitializeComponent();
    }

    /// <summary>
    /// Gets or sets the delegating sink that handles log events.
    /// </summary>
    /// <value>The delegating sink that handles log events.</value>
    public DelegatingSink<RichTextBlockSink> OutputLogSink
    {
        get => (DelegatingSink<RichTextBlockSink>)this.GetValue(OutputLogSinkProperty);
        set => this.SetValue(OutputLogSinkProperty, value);
    }

    /// <summary>
    /// Handles the <see cref="FrameworkElement.Loaded"/> event.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    /// <remarks>
    /// <para>
    /// This method sets up the <see cref="OutputLogSink"/> with the appropriate renderer and theme when the control is loaded.
    /// </para>
    /// </remarks>
    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        /* TODO: set this up only once when the control's template is applied */

        var appliedTheme = OutputLogThemes.Builtin[this.OutputLogSink.Theme ?? OutputLogThemes.Literate];

        var renderer = new OutputTemplateRenderer(
            appliedTheme,
            this.OutputLogSink.OutputTemplate,
            this.OutputLogSink.FormatProvider);
        this.OutputLogSink.DelegateSink = new RichTextBlockSink(this.RichTextBlock, renderer);
    }

    /// <summary>
    /// Handles the <see cref="FrameworkElement.Unloaded"/> event.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    /// <remarks>
    /// <para>
    /// This method cleans up the <see cref="OutputLogSink"/> when the control is unloaded.
    /// </para>
    /// </remarks>
    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.OutputLogSink.DelegateSink = null;
    }
}
