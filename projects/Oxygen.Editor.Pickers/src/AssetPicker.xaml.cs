// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Pickers;

/// <summary>
/// The editor's asset picker: a search box, an optional clearing choice, and the accepted assets grouped as Engine,
/// Packages and Content. It shows a <see cref="AssetPickerModel"/> and reports the picked choice; the owner assigns it.
/// </summary>
public sealed partial class AssetPicker : UserControl
{
    /// <summary>Identifies the <see cref="Model"/> dependency property.</summary>
    public static readonly DependencyProperty ModelProperty = Register<AssetPickerModel>(nameof(Model), defaultValue: null);

    /// <summary>Identifies the <see cref="ShowSearch"/> dependency property.</summary>
    public static readonly DependencyProperty ShowSearchProperty = Register<bool>(nameof(ShowSearch), defaultValue: true);

    /// <summary>Identifies the <see cref="EmptyText"/> dependency property.</summary>
    public static readonly DependencyProperty EmptyTextProperty = Register<string>(nameof(EmptyText), defaultValue: "No matching assets.");

    /// <summary>Identifies the <see cref="MaxListHeight"/> dependency property.</summary>
    public static readonly DependencyProperty MaxListHeightProperty = Register<double>(nameof(MaxListHeight), defaultValue: 420.0);

    /// <summary>Initializes a new instance of the <see cref="AssetPicker"/> class.</summary>
    /// <remarks>
    /// A picker opens with every choice: it clears its search when it leaves the visual tree, such as when its flyout
    /// closes. Clearing it while a flyout opens would change the choice lists while the flyout attaches them, which
    /// WinUI refuses.
    /// </remarks>
    public AssetPicker()
    {
        this.InitializeComponent();
        this.Unloaded += (_, _) =>
        {
            if (this.Model is { } model)
            {
                model.SearchText = string.Empty;
            }
        };
    }

    /// <summary>Raised when the user picks an enabled choice; a clearing choice has no asset.</summary>
    public event EventHandler<AssetChoice>? ChoiceInvoked;

    /// <summary>Gets or sets the choices the picker shows.</summary>
    public AssetPickerModel? Model { get => (AssetPickerModel?)this.GetValue(ModelProperty); set => this.SetValue(ModelProperty, value); }

    /// <summary>Gets or sets a value indicating whether the search box is shown.</summary>
    public bool ShowSearch { get => (bool)this.GetValue(ShowSearchProperty); set => this.SetValue(ShowSearchProperty, value); }

    /// <summary>Gets or sets the text shown when no choice matches, such as how to make one.</summary>
    public string EmptyText { get => (string)this.GetValue(EmptyTextProperty); set => this.SetValue(EmptyTextProperty, value); }

    /// <summary>Gets or sets the height above which the choices scroll.</summary>
    public double MaxListHeight { get => (double)this.GetValue(MaxListHeightProperty); set => this.SetValue(MaxListHeightProperty, value); }

    /// <summary>Converts a condition to the visibility of the element it controls.</summary>
    /// <param name="visible">Whether the element is shown.</param>
    /// <returns>The element's visibility.</returns>
    public static Visibility VisibleWhen(bool visible) => visible ? Visibility.Visible : Visibility.Collapsed;

    private static DependencyProperty Register<T>(string name, object? defaultValue)
        => DependencyProperty.Register(name, typeof(T), typeof(AssetPicker), new PropertyMetadata(defaultValue));

    private void OnChoiceClicked(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: AssetChoiceRow { Choice.IsEnabled: true } row })
        {
            this.ChoiceInvoked?.Invoke(this, row.Choice);
        }
    }

    private void OnClearClicked(object sender, RoutedEventArgs args)
    {
        if (this.Model?.Clear is { IsEnabled: true } clear)
        {
            this.ChoiceInvoked?.Invoke(this, clear);
        }
    }
}
