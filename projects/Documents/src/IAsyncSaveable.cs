// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Documents;

/// <summary>
///     Interface for documents that support asynchronous saving.
/// </summary>
public interface IAsyncSaveable
{
    /// <summary>
    ///     Saves the document asynchronously.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task SaveAsync();
}
