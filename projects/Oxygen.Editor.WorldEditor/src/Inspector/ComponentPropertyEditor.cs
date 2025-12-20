// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Base class for property editors for component sections in the details panel.
/// </summary>
public abstract partial class ComponentPropertyEditor : ObservableObject, IDetailsSection, IPropertyEditor<SceneNode>
{
    /// <inheritdoc />
    [ObservableProperty]
    public partial bool IsExpanded { get; set; } = true;

    /// <inheritdoc />
    public abstract string Header { get; }

    /// <inheritdoc />
    public abstract string Description { get; }

    /// <inheritdoc />
    public abstract void UpdateValues(ICollection<SceneNode> items);
}
