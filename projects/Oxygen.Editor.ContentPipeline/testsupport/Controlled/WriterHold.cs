// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>Holds the project's cook writer until released, so the requests behind it stay queued.</summary>
/// <remarks>The hold ends early when its project closes, as any operation of that project does.</remarks>
internal sealed class WriterHold
{
    private readonly TaskCompletionSource release = new(TaskCreationOptions.RunContinuationsAsynchronously);

    private WriterHold()
    {
    }

    /// <summary>Gets the operation holding the writer.</summary>
    public Task Held { get; private set; } = Task.CompletedTask;

    /// <summary>Takes the writer, which is free when no other operation holds it.</summary>
    /// <param name="coordinator">The project's cook coordinator.</param>
    /// <param name="cancellationToken">Cancels the hold.</param>
    /// <returns>The hold.</returns>
    public static WriterHold Take(ContentCookCoordinator coordinator, CancellationToken cancellationToken)
    {
        var hold = new WriterHold();
        hold.Held = coordinator.RunAsync(
            async (_, token) =>
            {
                await hold.release.Task.WaitAsync(token).ConfigureAwait(false);
                return 0;
            },
            cancellationToken);
        return hold;
    }

    /// <summary>Releases the writer to the queued requests.</summary>
    /// <returns>Completion after the holding operation ends.</returns>
    public async Task ReleaseAsync()
    {
        _ = this.release.TrySetResult();
        await this.Held.ConfigureAwait(false);
    }
}
