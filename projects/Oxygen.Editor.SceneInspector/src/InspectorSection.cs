// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.SceneInspector.Editing;

namespace Oxygen.Editor.SceneInspector;

/// <summary>A section of the Properties panel: a titled, collapsible group of fields that the panel shows or hides.</summary>
/// <remarks>
/// The panel enables a section's input while it shows it and disables it when it hides it, so a hidden section ends
/// its gestures without losing what it edits.
/// </remarks>
public abstract partial class InspectorSection : ObservableObject
{
    /// <summary>Gets or sets a value indicating whether the section is expanded.</summary>
    [ObservableProperty]
    public partial bool IsExpanded { get; set; } = true;

    /// <summary>Gets the section's header.</summary>
    public abstract string Header { get; }

    /// <summary>Gets the section's description.</summary>
    public abstract string Description { get; }

    /// <summary>Gets a value indicating whether the visible section can accept control input.</summary>
    internal bool IsInputEnabled { get; private set; } = true;

    /// <summary>Gets the feedback retained for the current authoring targets.</summary>
    internal virtual InspectorFieldDiagnostics? ValidationFeedback => null;

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
