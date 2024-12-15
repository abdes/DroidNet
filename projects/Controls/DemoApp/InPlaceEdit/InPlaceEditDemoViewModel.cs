// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// ViewModel for the <see cref="InPlaceEditDemoView"/> view.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public")]
public partial class InPlaceEditDemoViewModel : ObservableObject
{
    [ObservableProperty]
    private string label = "Hello World!";

    [ObservableProperty]
    private float numberValue = 0.0f;
}
