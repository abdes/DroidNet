// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Represents a transactional batch of settings operations.
/// Disposal triggers validation and commit.
/// </summary>
internal sealed class SettingsBatch : ISettingsBatch
{
    private static readonly AsyncLocal<SettingsBatch?> CurrentBatch = new();

    private readonly EditorSettingsManager manager;
    private readonly List<BatchItem> items = [];
    private readonly IProgress<SettingsProgress>? progress;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsBatch"/> class.
    /// </summary>
    /// <param name="manager">The settings manager that will commit this batch.</param>
    /// <param name="context">The context (scope) for all batch operations.</param>
    /// <param name="progress">Optional progress reporter.</param>
    /// <exception cref="InvalidOperationException">Thrown when attempting to create a nested batch.</exception>
    public SettingsBatch(EditorSettingsManager manager, SettingContext context, IProgress<SettingsProgress>? progress)
    {
        // Forbid nested batches - they don't work correctly with AsyncLocal in async contexts
        if (CurrentBatch.Value != null)
        {
            throw new InvalidOperationException(
                "Nested batches are not supported. Complete the current batch before starting a new one.");
        }

        this.manager = manager;
        this.Context = context;
        this.progress = progress;
        CurrentBatch.Value = this;
    }

    /// <inheritdoc/>
    public SettingContext Context { get; }

    /// <summary>
    /// Gets the currently active batch in the current execution context.
    /// </summary>
    internal static SettingsBatch? Current => CurrentBatch.Value;

    /// <inheritdoc/>
    public ISettingsBatch QueuePropertyChange<T>(SettingDescriptor<T> descriptor, T value)
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        this.items.Add(new BatchItem(
            descriptor.Key.SettingsModule,
            descriptor.Key.Name,
            value,
            this.Context.Scope,
            this.Context.ScopeId,
            descriptor));
        return this;
    }

    /// <inheritdoc/>
    public async ValueTask DisposeAsync()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        CurrentBatch.Value = null;

        if (this.items.Count == 0)
        {
            return;
        }

        await this.manager.CommitBatchAsync(this.items, this.progress, CancellationToken.None).ConfigureAwait(false);
    }
}
