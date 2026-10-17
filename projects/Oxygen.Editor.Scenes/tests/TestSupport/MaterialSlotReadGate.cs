// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// Holds the reads of a <see cref="FakeMaterialSlotProvider"/> until released.
/// </summary>
public sealed partial class MaterialSlotReadGate : IDisposable
{
    private readonly FakeMaterialSlotProvider owner;
    private readonly TaskCompletionSource release = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TaskCompletionSource waiting = new(TaskCreationOptions.RunContinuationsAsynchronously);

    internal MaterialSlotReadGate(FakeMaterialSlotProvider owner)
    {
        this.owner = owner;
    }

    /// <summary>Gets a task that completes once a read is waiting at the gate.</summary>
    public Task ReadPending => this.waiting.Task;

    /// <summary>Lets the waiting reads, and every later one, continue.</summary>
    public void Release()
    {
        this.owner.Released(this);
        _ = this.release.TrySetResult();
    }

    /// <inheritdoc/>
    public void Dispose() => this.Release();

    internal Task WaitAsync(CancellationToken cancellationToken)
    {
        _ = this.waiting.TrySetResult();
        return this.release.Task.WaitAsync(cancellationToken);
    }
}
