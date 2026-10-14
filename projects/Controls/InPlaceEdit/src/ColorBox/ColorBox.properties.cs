// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>Edits a red, green and blue color, with optional alpha, through a swatch picker and channel editors.</summary>
public partial class ColorBox
{
    /// <summary>Identifies the <see cref="Red"/> dependency property.</summary>
    public static readonly DependencyProperty RedProperty = Register<double>(nameof(Red), 0d, OnColorChanged);

    /// <summary>Identifies the <see cref="Green"/> dependency property.</summary>
    public static readonly DependencyProperty GreenProperty = Register<double>(nameof(Green), 0d, OnColorChanged);

    /// <summary>Identifies the <see cref="Blue"/> dependency property.</summary>
    public static readonly DependencyProperty BlueProperty = Register<double>(nameof(Blue), 0d, OnColorChanged);

    /// <summary>Identifies the <see cref="Alpha"/> dependency property.</summary>
    public static readonly DependencyProperty AlphaProperty = Register<double>(nameof(Alpha), 1d, OnColorChanged);

    /// <summary>Identifies the <see cref="RedIsIndeterminate"/> dependency property.</summary>
    public static readonly DependencyProperty RedIsIndeterminateProperty = Register<bool>(nameof(RedIsIndeterminate), defaultValue: false, OnColorChanged);

    /// <summary>Identifies the <see cref="GreenIsIndeterminate"/> dependency property.</summary>
    public static readonly DependencyProperty GreenIsIndeterminateProperty = Register<bool>(nameof(GreenIsIndeterminate), defaultValue: false, OnColorChanged);

    /// <summary>Identifies the <see cref="BlueIsIndeterminate"/> dependency property.</summary>
    public static readonly DependencyProperty BlueIsIndeterminateProperty = Register<bool>(nameof(BlueIsIndeterminate), defaultValue: false, OnColorChanged);

    /// <summary>Identifies the <see cref="AlphaIsIndeterminate"/> dependency property.</summary>
    public static readonly DependencyProperty AlphaIsIndeterminateProperty = Register<bool>(nameof(AlphaIsIndeterminate), defaultValue: false, OnColorChanged);

    /// <summary>Identifies the <see cref="HasAlpha"/> dependency property.</summary>
    public static readonly DependencyProperty HasAlphaProperty = Register<bool>(nameof(HasAlpha), defaultValue: false, OnShapeChanged);

    /// <summary>Identifies the <see cref="Encoding"/> dependency property.</summary>
    public static readonly DependencyProperty EncodingProperty = Register<ColorEncoding>(nameof(Encoding), ColorEncoding.Srgb, OnColorChanged);

    /// <summary>Identifies the <see cref="Range"/> dependency property.</summary>
    public static readonly DependencyProperty RangeProperty = Register<ColorRange>(nameof(Range), ColorRange.Normalized, OnRangeChanged);

    /// <summary>Identifies the <see cref="IsPickerVisible"/> dependency property.</summary>
    public static readonly DependencyProperty IsPickerVisibleProperty = Register<bool>(nameof(IsPickerVisible), defaultValue: true, OnShapeChanged);

    /// <summary>Identifies the <see cref="PickerAutomationName"/> dependency property.</summary>
    public static readonly DependencyProperty PickerAutomationNameProperty = Register<string>(nameof(PickerAutomationName), "Pick color");

    /// <summary>Identifies the <see cref="ChannelLabelPosition"/> dependency property.</summary>
    public static readonly DependencyProperty ChannelLabelPositionProperty = Register<LabelPosition>(nameof(ChannelLabelPosition), LabelPosition.Left, OnShapeChanged);

    /// <summary>Identifies the <see cref="ComponentMask"/> dependency property.</summary>
    public static readonly DependencyProperty ComponentMaskProperty = Register<string>(nameof(ComponentMask), "~.###");

    /// <summary>Gets or sets the red channel.</summary>
    public double Red { get => (double)this.GetValue(RedProperty); set => this.SetValue(RedProperty, value); }

    /// <summary>Gets or sets the green channel.</summary>
    public double Green { get => (double)this.GetValue(GreenProperty); set => this.SetValue(GreenProperty, value); }

    /// <summary>Gets or sets the blue channel.</summary>
    public double Blue { get => (double)this.GetValue(BlueProperty); set => this.SetValue(BlueProperty, value); }

    /// <summary>Gets or sets the straight alpha channel, edited when <see cref="HasAlpha"/> is set.</summary>
    public double Alpha { get => (double)this.GetValue(AlphaProperty); set => this.SetValue(AlphaProperty, value); }

    /// <summary>Gets or sets a value indicating whether the red channel shows a mixed value.</summary>
    public bool RedIsIndeterminate { get => (bool)this.GetValue(RedIsIndeterminateProperty); set => this.SetValue(RedIsIndeterminateProperty, value); }

    /// <summary>Gets or sets a value indicating whether the green channel shows a mixed value.</summary>
    public bool GreenIsIndeterminate { get => (bool)this.GetValue(GreenIsIndeterminateProperty); set => this.SetValue(GreenIsIndeterminateProperty, value); }

    /// <summary>Gets or sets a value indicating whether the blue channel shows a mixed value.</summary>
    public bool BlueIsIndeterminate { get => (bool)this.GetValue(BlueIsIndeterminateProperty); set => this.SetValue(BlueIsIndeterminateProperty, value); }

    /// <summary>Gets or sets a value indicating whether the alpha channel shows a mixed value.</summary>
    public bool AlphaIsIndeterminate { get => (bool)this.GetValue(AlphaIsIndeterminateProperty); set => this.SetValue(AlphaIsIndeterminateProperty, value); }

    /// <summary>Gets or sets a value indicating whether the color has an alpha channel to edit and pick.</summary>
    public bool HasAlpha { get => (bool)this.GetValue(HasAlphaProperty); set => this.SetValue(HasAlphaProperty, value); }

    /// <summary>Gets or sets how the red, green and blue channels are stored.</summary>
    public ColorEncoding Encoding { get => (ColorEncoding)this.GetValue(EncodingProperty); set => this.SetValue(EncodingProperty, value); }

    /// <summary>Gets or sets the range of the red, green and blue channels.</summary>
    public ColorRange Range { get => (ColorRange)this.GetValue(RangeProperty); set => this.SetValue(RangeProperty, value); }

    /// <summary>Gets or sets a value indicating whether the swatch picker shows; values that only scale other colors
    /// hide it.</summary>
    public bool IsPickerVisible { get => (bool)this.GetValue(IsPickerVisibleProperty); set => this.SetValue(IsPickerVisibleProperty, value); }

    /// <summary>Gets or sets the swatch's automation name and tooltip.</summary>
    public string PickerAutomationName { get => (string)this.GetValue(PickerAutomationNameProperty); set => this.SetValue(PickerAutomationNameProperty, value); }

    /// <summary>Gets or sets the side of each channel field its R, G, B or A label shows on.</summary>
    /// <remarks>
    ///     Channel labels always show inside their fields, so only <see cref="LabelPosition.Left"/> and
    ///     <see cref="LabelPosition.Right"/> apply; any other position shows them on the left.
    /// </remarks>
    public LabelPosition ChannelLabelPosition { get => (LabelPosition)this.GetValue(ChannelLabelPositionProperty); set => this.SetValue(ChannelLabelPositionProperty, value); }

    /// <summary>Gets or sets the channel editors' formatting mask.</summary>
    public string ComponentMask { get => (string)this.GetValue(ComponentMaskProperty); set => this.SetValue(ComponentMaskProperty, value); }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(ColorBox), new PropertyMetadata(defaultValue, changed));

    private static void OnColorChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((ColorBox)sender).OnColorChanged();

    private static void OnRangeChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var box = (ColorBox)sender;
        box.UpdateChannelRanges();
        box.OnColorChanged();
    }

    private static void OnShapeChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((ColorBox)sender).UpdateShape();
}
