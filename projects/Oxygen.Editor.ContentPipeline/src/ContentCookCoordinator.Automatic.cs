// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Applies the session pause to automatic requests before they acquire a writer.</summary>
public sealed partial class ContentCookCoordinator
{
    private TaskCompletionSource? automaticResume;

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc />
    public bool IsAutomaticCookingPaused
    {
        get
        {
            lock (this.stateLock)
            {
                return this.automaticResume is not null;
            }
        }

        set
        {
            TaskCompletionSource? resume;
            lock (this.stateLock)
            {
                if (value == (this.automaticResume is not null))
                {
                    return;
                }

                resume = this.automaticResume;
                this.automaticResume = value ? new(TaskCreationOptions.RunContinuationsAsynchronously) : null;
            }

            _ = resume?.TrySetResult();
            this.PropertyChanged?.Invoke(this, new(nameof(this.IsAutomaticCookingPaused)));
        }
    }

    private async Task WaitForAutomaticResumeAsync(CancellationToken cancellationToken)
    {
        Task resumed;
        lock (this.stateLock)
        {
            resumed = this.automaticResume?.Task ?? Task.CompletedTask;
        }

        await resumed.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private async Task AcquireWriterAsync(ContentCookOperation operation, bool automatic, CancellationToken cancellationToken)
    {
        while (true)
        {
            if (automatic)
            {
                await this.WaitForAutomaticResumeAsync(cancellationToken).ConfigureAwait(false);
            }

            await this.writer.WaitAsync(cancellationToken).ConfigureAwait(false);
            lock (this.stateLock)
            {
                if (automatic && this.automaticResume is not null)
                {
                    _ = this.writer.Release();
                    continue;
                }

                this.activeOperation = operation;
                return;
            }
        }
    }
}
