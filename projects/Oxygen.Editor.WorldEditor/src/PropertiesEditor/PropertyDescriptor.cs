// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class PropertyDescriptor : ObservableObject
{
    [ObservableProperty]
    private bool isEnabled = true;

    [ObservableProperty]
    private bool isVisible = true;

    public required string Name { get; init; }
}
