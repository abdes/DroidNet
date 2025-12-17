// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
///     Represents metadata and state for a property in the properties editor, including visibility and enabled state.
/// </summary>
public partial class PropertyDescriptor : ObservableObject
{
    /// <summary>
    ///     Gets the name of the property represented by this descriptor.
    /// </summary>
    public required string Name { get; init; }

    /// <summary>
    ///     Gets or sets a value indicating whether the property is enabled for editing.
    /// </summary>
    [ObservableProperty]
    public partial bool IsEnabled { get; set; } = true;

    /// <summary>
    ///     Gets or sets a value indicating whether the property is visible in the editor UI.
    /// </summary>
    [ObservableProperty]
    public partial bool IsVisible { get; set; } = true;
}
