// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Exposes the coordinator's cook history and scoped controls to the workspace.</summary>
public interface ICookRunService : System.ComponentModel.INotifyPropertyChanged
{
    /// <summary>Occurs when a run changes; callbacks may arrive on a worker thread.</summary>
    public event EventHandler<CookRunChangedEventArgs>? RunChanged;

    /// <summary>Refreshes consumers of any pipeline result before its run completes and releases the writer.</summary>
    public event CookCompletedHandler? CookCompleted;

    /// <summary>Gets or sets a value indicating whether automatic cooks wait in the queue; active and explicit cooks continue.</summary>
    public bool IsAutomaticCookingPaused { get; set; }

    /// <summary>Gets a consistent snapshot of this editor session's runs.</summary>
    public IReadOnlyList<CookRunSnapshot> Runs { get; }

    /// <summary>Requests cancellation of only the selected queued or active cook.</summary>
    /// <param name="operationId">The selected run.</param>
    /// <returns>Completion of cancellation signaling, not worker drain.</returns>
    public Task CancelAsync(Guid operationId);

    /// <summary>Requeues a blocked run after an explicit save action; capture rechecks all inputs.</summary>
    /// <param name="operationId">The blocked run.</param>
    /// <returns>Whether the run was waiting for saved inputs.</returns>
    public bool ResumeAfterSave(Guid operationId);
}
