// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.TabStrip;

/// <summary>
///     ViewModel for demonstrating the TabStrip control.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModels must be public")]
public partial class TabStripDemoViewModel(ILoggerFactory loggerFactory) : ObservableObject
{
    [ObservableProperty]
    public partial string NewItemHeader { get; set; } = "New Tab";

    [ObservableProperty]
    public partial bool NewItemIsPinned { get; set; } = false;

    [ObservableProperty]
    public partial bool NewItemIsClosable { get; set; } = true;

    [ObservableProperty]
    public partial double MaxItemWidth { get; set; } = 240.0;

    [ObservableProperty]
    public partial double PreferredItemWidth { get; set; } = 200.0;

    [ObservableProperty]
    public partial TabWidthPolicy TabWidthPolicy { get; set; } = TabWidthPolicy.Auto;

    [ObservableProperty]
    public partial bool ScrollOnWheel { get; set; } = true;

    [ObservableProperty]
    public partial string TabWidthPolicyName { get; set; } = TabWidthPolicy.Auto.ToString();

    [ObservableProperty]
    public partial Symbol? NewItemIconSymbol { get; set; }

    /// <summary>
    ///     Gets the logger factory.
    /// </summary>
    internal ILoggerFactory LoggerFactory { get; } = loggerFactory;

    partial void OnTabWidthPolicyNameChanged(string value)
    {
        if (Enum.TryParse(value, out TabWidthPolicy policy))
        {
            this.TabWidthPolicy = policy;
        }
    }
}
