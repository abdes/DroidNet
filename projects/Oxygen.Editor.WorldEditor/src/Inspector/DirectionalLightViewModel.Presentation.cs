// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector;

/// <summary>Separates section visibility from light binding and feedback lifetime.</summary>
public sealed partial class DirectionalLightViewModel
{
    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.edits?.Diagnostics;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);
}
