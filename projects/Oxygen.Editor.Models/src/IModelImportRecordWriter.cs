// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>
/// Saves a model's import record: replaces it, when it still holds what was read, and imports the model again with it,
/// as one operation of the project's one writer of model import records.
/// </summary>
public interface IModelImportRecordWriter
{
    /// <summary>Replaces a model's import record and reimports the model.</summary>
    /// <param name="model">The model's source.</param>
    /// <param name="expected">The bytes the record's file held when it was read or last saved.</param>
    /// <param name="replacement">The record to save.</param>
    /// <param name="cancellationToken">Cancels the request.</param>
    /// <returns>
    /// <see cref="DocumentSaveOutcome.Saved"/> once the record holds <paramref name="replacement"/>, even when the
    /// reimport then failed, which the model's import status reports; <see cref="DocumentSaveOutcome.Conflict"/> when the
    /// record changed since it was read; <see cref="DocumentSaveOutcome.Refused"/> when it could not be written.
    /// </returns>
    public Task<DocumentSaveResult> ReplaceAsync(AssetPath model, ReadOnlyMemory<byte> expected, ModelImportSettings replacement, CancellationToken cancellationToken);
}
