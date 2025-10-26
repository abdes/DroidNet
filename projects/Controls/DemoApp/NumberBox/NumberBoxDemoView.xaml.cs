// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// A simple demo page for NumberBox variants extracted from the InPlaceEdit demo.
/// </summary>
[ViewModel(typeof(NumberBoxDemoViewModel))]
public sealed partial class NumberBoxDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="NumberBoxDemoView"/> class.
    /// </summary>
    public NumberBoxDemoView()
    {
        this.InitializeComponent();

        this.Loaded += (_, _) =>
        {
            // Initialize validate handlers or other control setup here if needed.
            // Initialize control panel defaults
            this.MaskComboBox?.SetValue(ComboBox.SelectedIndexProperty, 1); // ~.##
            this.ConfigurableNumberBox?.SetValue(NumberBox.LabelProperty, "Configurable");
            this.ConfigurableNumberBox?.SetValue(NumberBox.NumberValueProperty, 42.5f);
        };
    }

    private void ToggleIndeterminate_Click(object sender, RoutedEventArgs args)
    {
        _ = sender; // Unused
        _ = args;   // Unused

        if (this.ViewModel is NumberBoxDemoViewModel vm)
        {
            vm.DemoIsIndeterminate = !vm.DemoIsIndeterminate;
        }
    }

    private void LabelPosition_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableNumberBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is string tag)
        {
            this.ConfigurableNumberBox.LabelPosition = Enum.Parse<LabelPosition>(tag);
        }
    }

    private void Mask_Changed(object sender, SelectionChangedEventArgs e)
    {
        _ = sender;
        _ = e;
        if (this.ConfigurableNumberBox is null || this.MaskComboBox is null)
        {
            return;
        }

        if (this.MaskComboBox.SelectedItem is ComboBoxItem item && item.Content is string mask)
        {
            this.ConfigurableNumberBox.Mask = mask;
        }
    }

    private void Multiplier_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableNumberBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is string tag && int.TryParse(tag, provider: null, out var multiplier))
        {
            this.ConfigurableNumberBox.Multiplier = multiplier;
        }
    }

    private void ValueAlignment_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableNumberBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is string tag)
        {
            this.ConfigurableNumberBox.HorizontalValueAlignment = Enum.Parse<TextAlignment>(tag);
        }
    }
}
