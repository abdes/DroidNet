// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// A demo page for the <see cref="VectorBox" /> control, showcasing various use cases and features.
/// </summary>
[ViewModel(typeof(VectorBoxDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Views wityh injectable ViewModel must be public")]
public sealed partial class VectorBoxDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="VectorBoxDemoView" /> class.
    /// </summary>
    public VectorBoxDemoView()
    {
        this.InitializeComponent();

        this.Loaded += (_, _) =>
        {
            // Validation for constrained vector (must be between -180 and 180)
            this.ValidatedVector.Validate += (_, e) =>
            {
                if (e.Target is Component component)
                {
                    e.IsValid = e.NewValue is <= 180.0f and >= -180.0f;
                }
            };

            // Custom masks for mixed mask vector
            this.MixedMaskVector.ComponentMasks["X"] = "±###.#";
            this.MixedMaskVector.ComponentMasks["Y"] = "##.##";
            this.MixedMaskVector.ComponentMasks["Z"] = "~.###";

            // Initialize control panel defaults
            this.VectorMaskComboBox?.SetValue(ComboBox.SelectedIndexProperty, 1); // ~.##
            this.ConfigurableVectorBox?.SetValue(VectorBox.XValueProperty, 1.0f);
            this.ConfigurableVectorBox?.SetValue(VectorBox.YValueProperty, 2.0f);
            this.ConfigurableVectorBox?.SetValue(VectorBox.ZValueProperty, 3.0f);
        };
    }

    private void ToggleXIndeterminate_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.ViewModel is VectorBoxDemoViewModel vm)
        {
            vm.IsIndeterminateX = !vm.IsIndeterminateX;
        }
    }

    private void ToggleYIndeterminate_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.ViewModel is VectorBoxDemoViewModel vm)
        {
            vm.IsIndeterminateY = !vm.IsIndeterminateY;
        }
    }

    private void ToggleZIndeterminate_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.ViewModel is VectorBoxDemoViewModel vm)
        {
            vm.IsIndeterminateZ = !vm.IsIndeterminateZ;
        }
    }

    private void Dimension_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableVectorBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is "2")
        {
            this.ConfigurableVectorBox.Dimension = 2;
        }
        else if (sender is RadioButton rb2 && rb2.Tag is "3")
        {
            this.ConfigurableVectorBox.Dimension = 3;
        }
    }

    private void LabelPosition_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableVectorBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is string tag)
        {
            if (Enum.TryParse<LabelPosition>(tag, out var position))
            {
                this.ConfigurableVectorBox.LabelPosition = position;
            }
        }
    }

    private void ComponentLabelPosition_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableVectorBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is string tag)
        {
            if (Enum.TryParse<LabelPosition>(tag, out var position))
            {
                this.ConfigurableVectorBox.ComponentLabelPosition = position;
            }
        }
    }

    private void VectorMask_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableVectorBox is null)
        {
            return;
        }

        if (sender is ComboBox cb && cb.SelectedItem is ComboBoxItem item)
        {
            if (item.Content is string mask)
            {
                this.ConfigurableVectorBox.ComponentMask = mask;
            }
        }
    }

    private void VectorMultiplier_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableVectorBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is string tag && int.TryParse(tag, provider: null, out var multiplier))
        {
            this.ConfigurableVectorBox.Multiplier = multiplier;
        }
    }

    private void VectorValueAlignment_Changed(object sender, RoutedEventArgs e)
    {
        _ = e;
        if (this.ConfigurableVectorBox is null)
        {
            return;
        }

        if (sender is RadioButton rb && rb.Tag is string tag)
        {
            this.ConfigurableVectorBox.HorizontalValueAlignment = Enum.Parse<TextAlignment>(tag);
        }
    }
}
