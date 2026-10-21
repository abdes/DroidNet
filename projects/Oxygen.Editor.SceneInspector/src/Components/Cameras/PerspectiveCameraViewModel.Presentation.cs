// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.SceneInspector.Editing;

namespace Oxygen.Editor.SceneInspector.Components.Cameras;

/// <summary>Separates section visibility from camera binding and feedback lifetime.</summary>
public sealed partial class PerspectiveCameraViewModel
{
    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.edits?.Diagnostics;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);
}
