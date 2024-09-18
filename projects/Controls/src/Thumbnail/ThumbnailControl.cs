// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Thumbnail;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Imaging;

public class ThumbnailControl : ContentControl
{
    public static readonly DependencyProperty IconSymbolProperty =
        DependencyProperty.Register(
            nameof(IconSymbol),
            typeof(Symbol?),
            typeof(ThumbnailControl),
            new PropertyMetadata(defaultValue: null, OnContentChanged));

    public static readonly DependencyProperty GlyphProperty =
        DependencyProperty.Register(
            nameof(Glyph),
            typeof(string),
            typeof(ThumbnailControl),
            new PropertyMetadata(defaultValue: null, OnContentChanged));

    public static readonly DependencyProperty ImagePathProperty =
        DependencyProperty.Register(
            nameof(ImagePath),
            typeof(string),
            typeof(ThumbnailControl),
            new PropertyMetadata(defaultValue: null, OnContentChanged));

    public static readonly DependencyProperty GeneratedImageProperty =
        DependencyProperty.Register(
            nameof(GeneratedImage),
            typeof(WriteableBitmap),
            typeof(ThumbnailControl),
            new PropertyMetadata(defaultValue: null, OnContentChanged));

    public Symbol? IconSymbol
    {
        get => (Symbol?)this.GetValue(IconSymbolProperty);
        set => this.SetValue(IconSymbolProperty, value);
    }

    public string? Glyph
    {
        get => (string)this.GetValue(GlyphProperty);
        set => this.SetValue(GlyphProperty, value);
    }

    public string? ImagePath
    {
        get => (string)this.GetValue(ImagePathProperty);
        set => this.SetValue(ImagePathProperty, value);
    }

    public WriteableBitmap? GeneratedImage
    {
        get => (WriteableBitmap)this.GetValue(GeneratedImageProperty);
        set => this.SetValue(GeneratedImageProperty, value);
    }

    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        this.UpdateContent();
    }

    private static void OnContentChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var control = (ThumbnailControl)sender;
        control.UpdateContent();
    }

    private void UpdateContent()
    {
        var border = new Border
        {
            BorderBrush = this.BorderBrush,
            BorderThickness = new Thickness(0), // Default to 0
        };

        if (this.IconSymbol.HasValue)
        {
            this.Content = new SymbolIcon(this.IconSymbol.Value);
        }
        else if (!string.IsNullOrEmpty(this.Glyph))
        {
            this.Content = new FontIcon { Glyph = this.Glyph };
        }
        else if (!string.IsNullOrEmpty(this.ImagePath))
        {
            this.Content = new Image { Source = new BitmapImage(new Uri(this.ImagePath)) };
        }
        else if (this.GeneratedImage != null)
        {
            this.Content = new Image { Source = this.GeneratedImage };
        }
        else
        {
            // Use a border for the empty thumbnail, styling will come from external resources.
            this.Content = new Border
            {
                Style = (Style)Application.Current.Resources["EmptyThumbnailBorderStyle"],
            };
        }
    }
}
