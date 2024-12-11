// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public abstract partial class ComponentPropertyEditor : ObservableObject, IDetailsSection, IPropertyEditor<GameEntity>
{
    [ObservableProperty]
    private bool isExpanded = true;

    /// <inheritdoc/>
    public abstract string Header { get; }

    /// <inheritdoc/>
    public abstract string Description { get; }

    /// <inheritdoc/>
    public abstract void UpdateValues(IList<GameEntity> items);
}
