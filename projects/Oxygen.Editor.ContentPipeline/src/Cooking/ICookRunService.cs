// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Exposes the coordinator's cook history and scoped controls to the workspace.</summary>
public interface ICookRunService
{
    /// <summary>Occurs when a run changes; callbacks may arrive on a worker thread.</summary>
    public event EventHandler<CookRunChangedEventArgs>? RunChanged;

    /// <summary>Refreshes consumers of any pipeline result before its run completes and releases the writer.</summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1003:Use generic event handler instances", Justification = "Publication awaits every consumer before releasing the writer; a void event cannot express completion.")]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0046:The delegate must return void", Justification = "Consumers return tasks so publication retains writer ownership until refresh completes.")]
    public event CookCompletedHandler? CookCompleted;

    /// <summary>Gets a consistent snapshot of this editor session's runs.</summary>
    public IReadOnlyList<CookRunSnapshot> Runs { get; }

    /// <summary>Requests cancellation of only the selected queued or active cook.</summary>
    /// <param name="operationId">The selected run.</param>
    /// <returns>Completion of cancellation signaling, not worker drain.</returns>
    public Task CancelAsync(Guid operationId);

    /// <summary>
    /// Waits until no operation holds or waits for the project writer: no cook, import, relocation or project change
    /// is in progress. A run waiting for a save holds neither.
    /// </summary>
    /// <param name="cancellationToken">Stops waiting.</param>
    /// <returns>Completion once the writer is idle; it may be taken again right after.</returns>
    public Task WhenWriterIdleAsync(CancellationToken cancellationToken);

    /// <summary>Requeues a blocked run after an explicit save action; capture rechecks all inputs.</summary>
    /// <param name="operationId">The blocked run.</param>
    /// <returns>Whether the run was waiting for saved inputs.</returns>
    public bool ResumeAfterSave(Guid operationId);
}
