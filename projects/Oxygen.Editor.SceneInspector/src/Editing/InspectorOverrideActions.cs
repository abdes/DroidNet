// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Controls.PropertyEditors;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>The commands a row offers besides reset, read when its context menu opens.</summary>
/// <param name="actions">Reads the commands.</param>
internal sealed class InspectorOverrideActions(Func<Task<IReadOnlyList<OverrideAction>>> actions) : IOverrideActionSource
{
    /// <inheritdoc/>
    public Task<IReadOnlyList<OverrideAction>> GetActionsAsync() => actions();
}
