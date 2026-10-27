// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Controls;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>
/// The override bar and reset of one field: how many of the edited nodes change its value from their source, and the
/// command that resets it to the source's value.
/// </summary>
/// <param name="reset">Resets the field's properties on the edited nodes.</param>
public sealed partial class InspectorFieldOverride(Action reset) : ObservableObject
{
    /// <summary>Gets how many of the edited nodes change the field's value from their source.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ResetCommand))]
    public partial OverrideState State { get; internal set; }

    private bool CanReset() => this.State != OverrideState.None;

    // Resets the field to the source's value on every edited node that changes it.
    [RelayCommand(CanExecute = nameof(CanReset))]
    private void Reset() => reset();
}
