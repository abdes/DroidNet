// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.ModelEditor;

/// <content>
/// Closing a model with unsaved import settings: the window's close asks to save, discard or cancel, as for any
/// document. A save replaces the import record and reimports; a discard leaves the record as it is.
/// </content>
public sealed partial class ModelEditorViewModel : IDocumentCloseParticipant
{
    /// <inheritdoc/>
    /// <remarks>Every edit is committed as it is made, so there is nothing to finish.</remarks>
    public Task PrepareForCloseAsync() => Task.CompletedTask;

    /// <inheritdoc/>
    public async Task<bool> SaveForCloseAsync()
        => !this.disposed && (await this.Document.SaveAsync(CancellationToken.None).ConfigureAwait(true)).Succeeded;

    /// <inheritdoc/>
    /// <remarks>The document area closes the document once its tab has closed, discarding unsaved settings.</remarks>
    public Task CloseAsync(bool discard) => Task.CompletedTask;

    /// <inheritdoc/>
    public void ResumeEditing()
    {
    }
}
