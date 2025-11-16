// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data;

/// <summary>
/// Manages the persistence and retrieval of module settings using a database context.
/// </summary>
public partial class EditorSettingsManager
{
    /// <summary>
    /// Creates a batch transaction for saving multiple settings atomically.
    /// Uses RAII pattern - dispose the batch to commit.
    /// </summary>
    /// <param name="context">The context (scope) for all batch operations. Defaults to Application scope.</param>
    /// <param name="progress">Optional progress reporter.</param>
    /// <returns>
    /// An <see cref="ISettingsBatch"/> instance representing the batch transaction.
    /// Dispose the returned object to commit the batch.
    /// </returns>
    public ISettingsBatch BeginBatch(SettingContext? context = null, IProgress<SettingsProgress>? progress = null)
        => new SettingsBatch(this, context ?? SettingContext.Application(), progress);

    /// <summary>
    /// Commits a batch of settings operations atomically.
    /// </summary>
    /// <param name="items">The batch items to commit.</param>
    /// <param name="progress">Optional progress reporter.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task that completes when the batch is committed.</returns>
    /// <exception cref="SettingsValidationException">Thrown when validation fails.</exception>
    internal async Task CommitBatchAsync(IReadOnlyList<BatchItem> items, IProgress<SettingsProgress>? progress, CancellationToken ct)
    {
        ValidateBatchItems(items);

        var transaction = await this.context.Database.BeginTransactionAsync(ct).ConfigureAwait(false);
        await using (transaction.ConfigureAwait(false))
        {
            try
            {
                var total = items.Count;
                var index = 0;
                foreach (var item in items)
                {
                    index++;
                    await this.ProcessBatchItemAsync(item.Module, item.Name, item.Value, item.Scope, item.ScopeId, ct).ConfigureAwait(false);
                    _ = await this.context.SaveChangesAsync(ct).ConfigureAwait(false);
                    progress?.Report(new SettingsProgress(total, index, item.Module, item.Name));
                }

                await transaction.CommitAsync(ct).ConfigureAwait(false);
            }
            catch
            {
                await transaction.RollbackAsync(ct).ConfigureAwait(false);
                throw;
            }
        }
    }
}
