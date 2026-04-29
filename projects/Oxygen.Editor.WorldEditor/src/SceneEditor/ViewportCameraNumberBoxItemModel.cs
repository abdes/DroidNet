// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// View-model data for a numeric camera menu row rendered with a DroidNet NumberBox.
/// </summary>
internal sealed class ViewportCameraNumberBoxItemModel : ObservableObject
{
    private readonly Action<float>? onNumberValueChanged;
    private float numberValue;

    /// <summary>
    /// Initializes a new instance of the <see cref="ViewportCameraNumberBoxItemModel"/> class.
    /// </summary>
    /// <param name="value">Initial numeric value.</param>
    /// <param name="minimum">Inclusive minimum accepted value.</param>
    /// <param name="maximum">Inclusive maximum accepted value, or <see cref="float.PositiveInfinity"/> for no upper bound.</param>
    /// <param name="unit">Unit label displayed next to the editor.</param>
    /// <param name="mask">DroidNet NumberBox display mask.</param>
    /// <param name="multiplier">DroidNet NumberBox adjustment multiplier.</param>
    /// <param name="width">Preferred NumberBox width inside the menu.</param>
    /// <param name="onNumberValueChanged">Optional callback used by the owning view model.</param>
    public ViewportCameraNumberBoxItemModel(
        float value,
        float minimum,
        float maximum,
        string unit,
        string mask = "~.##",
        int multiplier = 1,
        double width = 84,
        Action<float>? onNumberValueChanged = null)
    {
        if (float.IsNaN(minimum))
        {
            throw new ArgumentOutOfRangeException(nameof(minimum), "Minimum must be a number.");
        }

        if (float.IsNaN(maximum) || maximum < minimum)
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
    public float Minimum { get; }

    /// <summary>
    /// Gets the inclusive maximum accepted value, or <see cref="float.PositiveInfinity"/> for no upper bound.
    /// </summary>
    public float Maximum { get; }

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
    public int Multiplier { get; }

    /// <summary>
    /// Gets the preferred NumberBox width inside the menu.
    /// </summary>
    public double Width { get; }

    /// <summary>
    /// Gets or sets the edited numeric value.
    /// </summary>
    public float NumberValue
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

    /// <summary>
    /// Determines whether a candidate value is within the range accepted by the camera setting.
    /// </summary>
    /// <param name="value">The candidate value.</param>
    /// <returns><see langword="true"/> when the value is numeric and within range; otherwise, <see langword="false"/>.</returns>
    public bool IsInRange(float value) => !float.IsNaN(value) && value >= this.Minimum && value <= this.Maximum;
}
