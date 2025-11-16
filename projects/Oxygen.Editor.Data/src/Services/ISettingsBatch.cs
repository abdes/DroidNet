// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Represents a transactional batch of settings operations.
/// Uses RAII pattern: disposal commits the transaction.
/// </summary>
public interface ISettingsBatch : IAsyncDisposable
{
    /// <summary>
    /// Gets the context (scope) for all operations in this batch.
    /// </summary>
    public SettingContext Context { get; }

    /// <summary>
    /// Queues a property change in the batch.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="descriptor">The descriptor containing the key and validation rules.</param>
    /// <param name="value">The value to set for the specified setting.</param>
    /// <returns>
    /// Returns the current <see cref="ISettingsBatch"/> instance to allow method chaining.
    /// </returns>
    public ISettingsBatch QueuePropertyChange<T>(SettingDescriptor<T> descriptor, T value);
}
