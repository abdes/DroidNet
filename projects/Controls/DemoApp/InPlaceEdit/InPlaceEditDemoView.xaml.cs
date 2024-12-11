// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// A simple demo page for the <see cref="InPlaceEditableLabel"/> control.
/// </summary>
[ViewModel(typeof(InPlaceEditDemoViewModel))]
public sealed partial class InPlaceEditDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="InPlaceEditDemoView"/> class.
    /// </summary>
    public InPlaceEditDemoView()
    {
        this.InitializeComponent();

        this.Loaded += (_, _) => this.ValidatedInput.Validate += (_, e) =>
        {
            if (e.Text.Contains('a', StringComparison.OrdinalIgnoreCase))
            {
                e.IsValid = false;
            }
        };
    }
}
