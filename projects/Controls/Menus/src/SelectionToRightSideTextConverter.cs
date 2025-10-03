// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls;

/// <summary>
/// Converts selection state to right-side text display for menu items with semantic icons.
/// Combines accelerator text with selection indicators (checkmarks/radio bullets) on the right side.
/// </summary>
/// <remarks>
/// This converter is used to implement the UX pattern where:
/// - Items WITH semantic icons show selection indicators on the RIGHT side
/// - Items WITHOUT semantic icons use standard ToggleMenuFlyoutItem behavior (left side)
/// The converter uses the KeyboardAcceleratorTextOverride property to display selection state.
/// </remarks>
public partial class SelectionToRightSideTextConverter : IValueConverter
{
    /// <summary>
    /// Gets or sets the accelerator text (e.g., "Ctrl+S") to display alongside the selection indicator.
    /// </summary>
    public string? AcceleratorText { get; set; }

    /// <summary>
    /// Gets or sets the radio group identifier. When set, the item uses radio button selection (●/○).
    /// When null, the item uses checkbox selection (✓/blank).
    /// </summary>
    public string? RadioGroupId { get; set; }

    /// <inheritdoc/>
    public object? Convert(object value, Type targetType, object parameter, string language)
    {
        var isSelected = (bool)value;

        // Use checkmark for all selected items (both checkboxes and radio buttons)
        var selectionIndicator = isSelected ? " ✓" : string.Empty;

        // Combine accelerator text with selection indicator
        if (!string.IsNullOrEmpty(this.AcceleratorText))
        {
            return $"{this.AcceleratorText}{selectionIndicator}";
        }

        // If no accelerator text, just return the selection indicator (trimmed)
        return selectionIndicator.TrimStart();
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException("SelectionToRightSideTextConverter is a one-way converter.");
}
