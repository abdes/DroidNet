// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// ViewModel for the <see cref="NumberBoxDemoView"/>.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public")]
public partial class NumberBoxDemoViewModel : ObservableObject
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DemoNullableValue))]
    public partial float NumberValue { get; set; } = -20.0f;

    [ObservableProperty]
    public partial float RotationX { get; set; } = 0.0f;

    [ObservableProperty]
    public partial float RotationY { get; set; } = 0.0f;

    [ObservableProperty]
    public partial float RotationZ { get; set; } = 0.0f;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DemoNullableValue))]
    public partial bool DemoIsIndeterminate { get; set; } = false;

    /// <summary>
    /// Gets or sets derived nullable value: null when indeterminate, otherwise the numeric value.
    /// Setting this keeps the other mapped properties in sync.
    /// </summary>
    public float? DemoNullableValue
    {
        get => this.DemoIsIndeterminate ? null : this.NumberValue;
        set
        {
            if (value.HasValue)
            {
                // set numeric and clear indeterminate
                this.NumberValue = value.Value;
                this.DemoIsIndeterminate = false;
            }
            else
            {
                // set indeterminate; do not modifiy numeric value
                this.DemoIsIndeterminate = true;
            }

            this.OnPropertyChanged(nameof(this.DemoNullableValue));
        }
    }
}
