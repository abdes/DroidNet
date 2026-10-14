// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>Edits a red, green and blue color, with optional alpha, through a swatch picker and channel editors.</summary>
/// <remarks>
///     <para>
///     The channels are stored as <see cref="Encoding"/> and <see cref="Range"/> say; the swatch and the picker show the
///     display color. Each channel is a <see cref="NumberBox"/> of an inner <see cref="VectorBox"/>, labelled R, G, B
///     and A.
///     </para>
///     <para>
///     A pick is one edit session, reported like a channel edit through <see cref="EditSessionStarted"/> and
///     <see cref="EditSessionCompleted"/> with no channel. Each picked color is offered through
///     <see cref="ColorPicked"/>; a handler can apply it as one model edit, otherwise the box sets its channels. Escape
///     cancels a pick and restores the channels the box set.
///     </para>
/// </remarks>
/// <example>
///     <code lang="xaml"><![CDATA[
///     <controls:ColorBox
///         Encoding="Linear"
///         HasAlpha="True"
///         Red="{x:Bind ViewModel.Red, Mode=TwoWay}"
///         Green="{x:Bind ViewModel.Green, Mode=TwoWay}"
///         Blue="{x:Bind ViewModel.Blue, Mode=TwoWay}"
///         Alpha="{x:Bind ViewModel.Alpha, Mode=TwoWay}" />
///     ]]></code>
/// </example>
[TemplatePart(Name = SwatchButtonPartName, Type = typeof(Button))]
[TemplatePart(Name = SwatchFillPartName, Type = typeof(Border))]
[TemplatePart(Name = SwatchMixedPartName, Type = typeof(UIElement))]
[TemplatePart(Name = SwatchAlphaPartName, Type = typeof(UIElement))]
[TemplatePart(Name = SwatchAlphaFillPartName, Type = typeof(Border))]
[TemplatePart(Name = ChannelsPartName, Type = typeof(VectorBox))]
public partial class ColorBox : Control
{
    private const string SwatchButtonPartName = "PartSwatchButton";
    private const string SwatchFillPartName = "PartSwatchFill";
    private const string SwatchMixedPartName = "PartSwatchMixed";
    private const string SwatchAlphaPartName = "PartSwatchAlpha";
    private const string SwatchAlphaFillPartName = "PartSwatchAlphaFill";
    private const string ChannelsPartName = "PartChannels";

    private Button? swatchButton;
    private Border? swatchFill;
    private UIElement? swatchMixed;
    private UIElement? swatchAlpha;
    private Border? swatchAlphaFill;
    private VectorBox? channels;
    private Flyout? flyout;
    private ColorPicker? picker;
    private Pick? pick;
    private double pickIntensity = 1d;
    private bool isSyncingPicker;

    /// <summary>Initializes a new instance of the <see cref="ColorBox"/> class.</summary>
    public ColorBox()
    {
        this.DefaultStyleKey = typeof(ColorBox);
        this.Unloaded += this.OnUnloaded;
        _ = this.RegisterPropertyChangedCallback(AutomationProperties.NameProperty, (_, _) => this.UpdateChannelsName());
    }

    /// <summary>Occurs when a channel edit or a pick starts.</summary>
    public event EventHandler<ColorBoxEditSessionEventArgs>? EditSessionStarted;

    /// <summary>Occurs when a channel edit or a pick commits or is canceled.</summary>
    public event EventHandler<ColorBoxEditSessionEventArgs>? EditSessionCompleted;

    /// <summary>Occurs when a typed or dragged channel value is validated, after the channel's mask and range accept
    /// it; <see cref="ValidationEventArgs{T}.Target"/> is the channel's <see cref="Component"/>.</summary>
    public event EventHandler<ValidationEventArgs<double>>? Validate;

    /// <summary>Occurs for each color picked during a pick, before the box applies it.</summary>
    public event EventHandler<ColorBoxColorPickedEventArgs>? ColorPicked;

    /// <summary>Gets the red, green, blue and alpha channels.</summary>
    public ColorChannels Channels => new(this.Red, this.Green, this.Blue, this.Alpha);

    private bool IsMixed => this.RedIsIndeterminate || this.GreenIsIndeterminate || this.BlueIsIndeterminate;

    /// <summary>Names a channel: R, G, B or A.</summary>
    /// <param name="channel">The channel's vector component.</param>
    /// <returns>The channel name.</returns>
    public static string ChannelName(Component channel)
        => channel switch
        {
            Component.X => "R",
            Component.Y => "G",
            Component.Z => "B",
            Component.W => "A",
            _ => throw new ArgumentOutOfRangeException(nameof(channel), channel, "Unknown color channel."),
        };

    /// <summary>Closes the picker, committing an open pick.</summary>
    public void HidePicker() => this.flyout?.Hide();

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        if (this.channels is not null)
        {
            this.channels.EditSessionStarted -= this.OnChannelEditStarted;
            this.channels.EditSessionCompleted -= this.OnChannelEditCompleted;
            this.channels.Validate -= this.OnChannelValidate;
        }

        this.swatchButton?.Flyout = null;

        base.OnApplyTemplate();

        this.swatchButton = this.GetTemplateChild(SwatchButtonPartName) as Button;
        this.swatchFill = this.GetTemplateChild(SwatchFillPartName) as Border;
        this.swatchMixed = this.GetTemplateChild(SwatchMixedPartName) as UIElement;
        this.swatchAlpha = this.GetTemplateChild(SwatchAlphaPartName) as UIElement;
        this.swatchAlphaFill = this.GetTemplateChild(SwatchAlphaFillPartName) as Border;
        this.channels = this.GetTemplateChild(ChannelsPartName) as VectorBox;

        if (this.channels is not null)
        {
            foreach (var channel in (ReadOnlySpan<Component>)[Component.X, Component.Y, Component.Z, Component.W])
            {
                this.channels.ComponentLabels[channel] = ChannelName(channel);
            }

            this.channels.ComponentLabelForegrounds[Component.X] = new SolidColorBrush(Colors.Red);
            this.channels.ComponentLabelForegrounds[Component.Y] = new SolidColorBrush(Colors.Green);
            this.channels.ComponentLabelForegrounds[Component.Z] = new SolidColorBrush(Colors.Blue);
            this.channels.EditSessionStarted += this.OnChannelEditStarted;
            this.channels.EditSessionCompleted += this.OnChannelEditCompleted;
            this.channels.Validate += this.OnChannelValidate;
            this.UpdateChannelsName();
            this.UpdateChannelRanges();
        }

        if (this.swatchButton is not null)
        {
            this.flyout ??= this.CreateFlyout();
            this.swatchButton.Flyout = this.flyout;
        }

        this.UpdateShape();
    }

    private void OnColorChanged()
    {
        this.UpdateSwatch();
        if (this.pick is null)
        {
            this.SyncPicker();
        }
    }

    // The channels carry the box's name, so each channel reads as part of this color.
    private void UpdateChannelsName()
    {
        if (this.channels is not null)
        {
            AutomationProperties.SetName(this.channels, AutomationProperties.GetName(this));
        }
    }

    // Typed and dragged channels stay in range. Normalized channels drag in hundredths, unbounded ones in tenths;
    // alpha is always 0 to 1.
    private void UpdateChannelRanges()
    {
        if (this.channels is null)
        {
            return;
        }

        var normalized = this.Range == ColorRange.Normalized;
        this.channels.Minimum = 0d;
        this.channels.Maximum = normalized ? 1d : double.PositiveInfinity;
        this.channels.Multiplier = normalized ? 0.01d : 0.1d;
        this.channels.SetComponentRange(Component.W, 0d, 1d);
    }

    private void UpdateShape()
    {
        if (this.channels is not null)
        {
            this.channels.Dimension = this.HasAlpha ? 4 : 3;
            this.channels.ComponentLabelPosition = this.ChannelLabelPosition == LabelPosition.Right ? LabelPosition.Right : LabelPosition.Left;
        }

        this.swatchButton?.Visibility = this.IsPickerVisible ? Visibility.Visible : Visibility.Collapsed;

        if (!this.IsPickerVisible)
        {
            this.HidePicker();
        }

        this.UpdateSwatch();
    }

    private void UpdateSwatch()
    {
        if (this.swatchFill is null)
        {
            return;
        }

        var mixed = this.IsMixed;
        var display = ColorPresentation.ToDisplayColor(this.ShownColor(), this.Encoding, this.Range);
        var showsAlpha = this.HasAlpha && !mixed && !this.AlphaIsIndeterminate;
        this.swatchFill.Background = mixed ? null : new SolidColorBrush(display with { A = 255 });
        Grid.SetColumnSpan(this.swatchFill, showsAlpha ? 1 : 2);
        this.swatchMixed?.Visibility = mixed ? Visibility.Visible : Visibility.Collapsed;

        this.swatchAlpha?.Visibility = showsAlpha ? Visibility.Visible : Visibility.Collapsed;

        this.swatchAlphaFill?.Background = showsAlpha ? new SolidColorBrush(display) : null;
    }

    private Flyout CreateFlyout()
    {
        var created = new Flyout();
        created.Opening += this.OnFlyoutOpening;

        // Closing the picker finishes the pick, so the edit is complete as soon as the flyout closes.
        created.Closed += (_, _) => this.EndPick(NumberBoxEditCompletionKind.Commit);
        return created;
    }

    private void OnFlyoutOpening(object? sender, object args)
    {
        if (this.picker is null)
        {
            this.picker = new ColorPicker { IsColorChannelTextInputVisible = true, IsHexInputVisible = true };
            this.picker.ColorChanged += this.OnPickerColorChanged;
            this.AttachPickerGestures(this.picker);
            this.flyout!.Content = this.picker;
        }

        this.picker.IsAlphaEnabled = this.HasAlpha;
        this.picker.IsAlphaSliderVisible = this.HasAlpha;
        this.picker.IsAlphaTextInputVisible = this.HasAlpha;
        this.SyncPicker();
    }

    private void SyncPicker()
    {
        if (this.picker is null)
        {
            return;
        }

        this.pickIntensity = ColorPresentation.Intensity(this.Channels, this.Range);
        this.isSyncingPicker = true;
        try
        {
            this.picker.Color = ColorPresentation.ToDisplayColor(this.ShownColor(), this.Encoding, this.pickIntensity);
        }
        finally
        {
            this.isSyncingPicker = false;
        }
    }

    private ColorChannels ShownColor() => this.HasAlpha ? this.Channels : this.Channels with { Alpha = 1d };

    private void OnPickerColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        if (this.isSyncingPicker || !this.IsPickerVisible)
        {
            return;
        }

        var picked = ColorPresentation.ApplyPick(this.ShownColor(), args.NewColor, this.Encoding, this.pickIntensity);
        if (!this.HasAlpha)
        {
            picked = picked with { Alpha = this.Alpha };
        }

        if (picked == this.Channels)
        {
            return;
        }

        // ColorChanged may run before the handled PointerPressed reaches the picker.
        this.BeginPick(NumberBoxEditInteractionKind.PointerDrag);
        var request = new ColorBoxColorPickedEventArgs(picked);
        this.ColorPicked?.Invoke(this, request);
        if (!request.Handled && this.pick is { } active)
        {
            active.AppliedHere = true;
            this.SetChannels(picked);
        }
    }

    private void SetChannels(ColorChannels color)
        => (this.Red, this.Green, this.Blue, this.Alpha) = (color.Red, color.Green, color.Blue, color.Alpha);

    private void BeginPick(NumberBoxEditInteractionKind interaction)
    {
        if (this.pick is not null)
        {
            return;
        }

        this.pick = new Pick(interaction, this.Channels);
        this.EditSessionStarted?.Invoke(this, new ColorBoxEditSessionEventArgs(channel: null, interaction));
    }

    private void EndPick(NumberBoxEditCompletionKind completion)
    {
        if (this.pick is not { } ended)
        {
            return;
        }

        this.pick = null;
        if (completion == NumberBoxEditCompletionKind.Cancel && ended.AppliedHere)
        {
            this.SetChannels(ended.Start);
        }

        this.EditSessionCompleted?.Invoke(this, new ColorBoxEditSessionEventArgs(channel: null, ended.Interaction, completion));
        this.SyncPicker();
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        this.EndPick(NumberBoxEditCompletionKind.Commit);
        this.HidePicker();
    }

    private void OnChannelEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.EditSessionStarted?.Invoke(this, new ColorBoxEditSessionEventArgs(args.Component, args.InteractionKind, args.CompletionKind, args.InputText));

    private void OnChannelEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.EditSessionCompleted?.Invoke(this, new ColorBoxEditSessionEventArgs(args.Component, args.InteractionKind, args.CompletionKind, args.InputText));

    private void OnChannelValidate(object? sender, ValidationEventArgs<double> args) => this.Validate?.Invoke(this, args);

    private sealed class Pick(NumberBoxEditInteractionKind interaction, ColorChannels start)
    {
        public NumberBoxEditInteractionKind Interaction { get; } = interaction;

        public ColorChannels Start { get; } = start;

        public bool AppliedHere { get; set; }
    }
}
