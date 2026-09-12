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

    /// <summary>Gets a value indicating whether the visible section can accept control input.</summary>
    internal bool IsInputEnabled { get; private set; } = true;

    /// <summary>Gets the feedback retained for the current authoring targets.</summary>
    internal virtual InspectorFieldDiagnostics? ValidationFeedback => null;

    /// <inheritdoc />
    public abstract void UpdateValues(ICollection<SceneNode> items);

    /// <summary>Changes input availability without discarding the current targets or feedback.</summary>
    /// <param name="enabled">Whether the section is visible and can accept new input.</param>
    internal void SetInputEnabled(bool enabled)
    {
        if (this.IsInputEnabled == enabled)
        {
            return;
        }

        this.IsInputEnabled = enabled;
        this.OnInputEnabledChanged(enabled);
    }

    /// <summary>Ends obsolete control gestures when a section becomes hidden.</summary>
    /// <param name="enabled">Whether input is enabled.</param>
    protected virtual void OnInputEnabledChanged(bool enabled)
    {
    }
}
