// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// View-model data for a numeric camera setting edited with a DroidNet NumberBox.
/// </summary>
public sealed class ViewportCameraNumberBoxItemModel : ObservableObject
{
    private readonly Action<double>? onNumberValueChanged;
    private double numberValue;

    /// <summary>
    /// Initializes a new instance of the <see cref="ViewportCameraNumberBoxItemModel"/> class.
    /// </summary>
    /// <param name="value">Initial numeric value.</param>
    /// <param name="minimum">Inclusive minimum accepted value.</param>
    /// <param name="maximum">Inclusive maximum accepted value, or <see cref="double.PositiveInfinity"/> for no upper bound.</param>
    /// <param name="unit">Unit label displayed next to the editor.</param>
    /// <param name="mask">DroidNet NumberBox display mask.</param>
    /// <param name="multiplier">DroidNet NumberBox adjustment multiplier.</param>
    /// <param name="width">Preferred NumberBox width inside the flyout.</param>
    /// <param name="onNumberValueChanged">Optional callback used by the owning view model.</param>
    public ViewportCameraNumberBoxItemModel(
        double value,
        double minimum,
        double maximum,
        string unit,
        string mask = "~.##",
        double multiplier = 1d,
        double width = 84,
        Action<double>? onNumberValueChanged = null)
    {
        if (double.IsNaN(minimum))
        {
            throw new ArgumentOutOfRangeException(nameof(minimum), "Minimum must be a number.");
        }

        if (double.IsNaN(maximum) || maximum < minimum)
        {
            throw new ArgumentOutOfRangeException(nameof(maximum), "Maximum must be a number greater than or equal to minimum.");
        }

        this.Minimum = minimum;
        this.Maximum = maximum;
        this.Unit = unit;
        this.Mask = mask;
        this.Multiplier = multiplier;
        this.Width = width;
        this.onNumberValueChanged = onNumberValueChanged;
        this.numberValue = value;
    }

    /// <summary>
    /// Gets the inclusive minimum accepted value.
    /// </summary>
    public double Minimum { get; }

    /// <summary>
    /// Gets the inclusive maximum accepted value, or <see cref="double.PositiveInfinity"/> for no upper bound.
    /// </summary>
    public double Maximum { get; }

    /// <summary>
    /// Gets the unit label displayed next to the number box.
    /// </summary>
    public string Unit { get; }

    /// <summary>
    /// Gets the DroidNet NumberBox display mask.
    /// </summary>
    public string Mask { get; }

    /// <summary>
    /// Gets the DroidNet NumberBox adjustment multiplier.
    /// </summary>
    public double Multiplier { get; }

    /// <summary>
    /// Gets the preferred NumberBox width inside the flyout.
    /// </summary>
    public double Width { get; }

    /// <summary>
    /// Gets or sets the edited numeric value.
    /// </summary>
    public double NumberValue
    {
        get => this.numberValue;
        set
        {
            if (this.SetProperty(ref this.numberValue, value))
            {
                this.onNumberValueChanged?.Invoke(value);
            }
        }
    }
}
