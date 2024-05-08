// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Controls;

using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Storage;

/// <summary>
/// An interactive control to display a known location for project storage. Similar
/// to a <see cref="Button" />, it can be clicked and can get focus.
/// </summary>
/// <para>
/// The control shows an icon corresponding to the known location, which is
/// automatically set. It can also be set from outside through the
/// <see cref="IconGlyph" /> property.
/// </para>
/// <para>
/// This control also support selection behavior through the
/// <see cref="IsSelected" /> property. When selected, it will have an outer
/// border.
/// </para>
public sealed partial class KnownLocationButton
{
    public static readonly DependencyProperty LocationProperty = DependencyProperty.Register(
        nameof(Location),
        typeof(KnownLocation),
        typeof(KnownLocationButton),
        new PropertyMetadata(default));

    public static readonly DependencyProperty IconGlyphProperty = DependencyProperty.Register(
        nameof(IconGlyph),
        typeof(string),
        typeof(KnownLocationButton),
        new PropertyMetadata(default(string)));

    public static readonly DependencyProperty IsSelectedProperty = DependencyProperty.Register(
        nameof(IsSelected),
        typeof(bool),
        typeof(KnownLocationButton),
        new PropertyMetadata(default(bool)));

    public KnownLocationButton() => this.InitializeComponent();

    [Browsable(true)]
    [Category("Action")]
    [Description("Invoked when user clicks the location")]
    public event EventHandler<KnownLocation>? Click;

    public KnownLocation? Location
    {
        get => (KnownLocation)this.GetValue(LocationProperty);
        set
        {
            if (value == null)
            {
                return;
            }

            this.SetValue(LocationProperty, value);
            switch (value.Key)
            {
                case KnownLocations.RecentProjects:
                    this.IconGlyph = "\uE823";
                    return;
                case KnownLocations.Documents:
                    this.IconGlyph = "\uEC25";
                    return;
                case KnownLocations.Desktop:
                    this.IconGlyph = "\uE8b7";
                    return;
                case KnownLocations.Downloads:
                    this.IconGlyph = "\uF012";
                    return;
                case KnownLocations.ThisComputer:
                    this.IconGlyph = "\uEC4E";
                    return;
                case KnownLocations.OneDrive:
                    this.IconGlyph = "\uEC27";
                    return;
                default:
                    Debug.WriteLine($"Unexpected `KnownLocationKey` value `{value.Key}`");

                    // Treat as an unknown location
                    break;
            }

            this.IconGlyph = "\uE8B7";
        }
    }

    public string IconGlyph
    {
        get => (string)this.GetValue(IconGlyphProperty);
        set => this.SetValue(IconGlyphProperty, value);
    }

    public bool IsSelected
    {
        get => (bool)this.GetValue(IsSelectedProperty);
        set
        {
            this.SetValue(IsSelectedProperty, value);

            this.TheButton.BorderBrush = value
                ? this.Resources["SelectedOuterBorderBrush"] as Brush
                : this.Resources["NormalOuterBorderBrush"] as Brush;
        }
    }

    private void OnClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        Debug.WriteLine("Location button clicked");
        var documentLocation = this.Location;
        if (documentLocation != null)
        {
            this.Click?.Invoke(this, documentLocation);
        }
    }
}
