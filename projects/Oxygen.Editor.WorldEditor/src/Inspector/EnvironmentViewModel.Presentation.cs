// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector;

/// <summary>Separates section visibility from environment input ownership.</summary>
public partial class EnvironmentViewModel
{
    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.fieldDiagnostics;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled)
    {
        this.edits?.SetInputEnabled(enabled);
        this.lightAssignments?.SetInputEnabled(enabled);
    }
}
