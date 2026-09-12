// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Ends hidden transform gestures without discarding field feedback.</summary>
public sealed partial class TransformViewModel
{
    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.diagnostics;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled)
    {
        if (!enabled)
        {
            foreach (var field in this.activeSessions.Keys.ToArray())
            {
                this.supersededFields.Add(field);
                _ = this.CompleteActiveSessionAsync(field, NumberBoxEditCompletionKind.Cancel);
            }
        }
    }
}
