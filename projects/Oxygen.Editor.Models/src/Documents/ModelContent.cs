// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.TimeMachine;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models.Documents;

/// <summary>What a model document shows and edits: the model, named by its source file, and its import settings.</summary>
/// <remarks>
/// <para>
/// The model's structure and values are read-only. The scene that previews it is built by the scenes module from
/// <see cref="Source"/>, as any scene instances a model.
/// </para>
/// <para>
/// What the document edits is the model's import record: its settings and its node continuity, each change one step of
/// the document's history. Save replaces the record and reimports the model, as one operation of the record's writer;
/// the editor never writes the source file. What an import or a relocation rewrote in the record is taken with
/// <see cref="TakeRecord"/>, keeping unsaved edits over it.
/// </para>
/// </remarks>
public sealed class ModelContent : IDocumentContent
{
    private readonly HistoryKeeper? history;
    private readonly IModelImportRecordWriter? writer;
    private ModelImportSettings? saved;

    /// <summary>Initializes a new instance of the <see cref="ModelContent"/> class.</summary>
    /// <param name="source">The model's source file, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <param name="record">The model's import record as read, or <see langword="null"/> when the model is not imported.</param>
    /// <param name="history">The document's history, which records each edit; none for a content that takes no edits.</param>
    /// <param name="writer">Saves the record; none for a content that is never saved.</param>
    public ModelContent(AssetPath source, ModelImportSettings? record = null, HistoryKeeper? history = null, IModelImportRecordWriter? writer = null)
    {
        ArgumentNullException.ThrowIfNull(source);
        this.Source = source;
        this.saved = record;
        this.Record = record;
        this.history = history;
        this.writer = writer;
    }

    /// <summary>Occurs when the model's source file moved or was renamed, after <see cref="Source"/> follows it.</summary>
    public event EventHandler<ModelRelocatedEventArgs>? Relocated;

    /// <summary>Occurs when the record changed: an edit, its undo or redo, or what <see cref="TakeRecord"/> took.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the model's source file, which identifies the model and is what a scene places.</summary>
    public AssetPath Source { get; private set; }

    /// <summary>Gets the model's import record with this document's edits, or <see langword="null"/> when the model is not
    /// imported.</summary>
    public ModelImportSettings? Record { get; private set; }

    /// <summary>Gets the model's import record as its file holds it, which the last save wrote or the last read found.</summary>
    public ModelImportSettings? SavedRecord => this.saved;

    /// <inheritdoc/>
    public bool HasEditsInProgress => false;

    /// <summary>Changes the model's import settings, as one step of the document's history.</summary>
    /// <param name="settings">The settings, in the import manifest's vocabulary.</param>
    /// <param name="label">The history label, such as "Edit Units".</param>
    /// <exception cref="InvalidOperationException">The model has no import record.</exception>
    public void EditSettings(JsonElement settings, string label)
        => this.Change(this.RequireRecord().WithSettings(settings), label);

    /// <summary>Makes the node with <paramref name="anchor"/> continue the node with <paramref name="continues"/>, as one
    /// step of the document's history.</summary>
    /// <param name="anchor">The continuing node's anchor.</param>
    /// <param name="continues">The anchor of the node it continues.</param>
    /// <exception cref="ArgumentException">The anchors are equal, or <paramref name="anchor"/> already continues a node.</exception>
    public void AddContinuity(string anchor, string continues)
        => this.Change(this.RequireRecord().WithContinuity(anchor, continues), $"Continue {continues}");

    /// <summary>Removes the node continuity entry of <paramref name="anchor"/>, as one step of the document's history.</summary>
    /// <param name="anchor">The continuing node's anchor.</param>
    public void RemoveContinuity(string anchor)
        => this.Change(this.RequireRecord().WithoutContinuity(anchor), $"Remove the continuity of {anchor}");

    /// <summary>Takes the record as its file holds it now, after an import or a relocation rewrote it.</summary>
    /// <param name="latest">The record as read from its file, or <see langword="null"/> when no record names the model.</param>
    /// <param name="keepEdits">Whether the document's unsaved settings and node continuity stay over the file's.</param>
    public void TakeRecord(ModelImportSettings? latest, bool keepEdits)
    {
        this.saved = latest;
        this.Record = keepEdits && latest is not null && this.Record is { } edited ? edited.WithEditsOver(latest) : latest;
        this.Changed?.Invoke(this, EventArgs.Empty);
    }

    /// <inheritdoc/>
    public Task FinishEditsAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <inheritdoc/>
    /// <remarks>The capture replaces the model's import record and reimports the model; it never writes the source file.</remarks>
    public DocumentCapture Capture()
    {
        if (this.saved is not { } expected || this.Record is not { } record || this.writer is not { } records)
        {
            return DocumentCapture.Refused($"'{this.Source.Name}' has no import record to save; import it first.");
        }

        var source = this.Source;
        return DocumentCapture.Stored(async cancellationToken =>
        {
            var result = await records.ReplaceAsync(source, expected, record, cancellationToken).ConfigureAwait(true);
            if (result.Succeeded)
            {
                this.saved = record;
            }

            return result;
        });
    }

    /// <inheritdoc/>
    public DocumentCapture CaptureCopy(DocumentFile copy) => DocumentCapture.Refused("A model is copied as its source file, outside the editor.");

    /// <inheritdoc/>
    /// <remarks>The model follows its source file to its new name.</remarks>
    public void ApplyRelocation(DocumentRelocation relocation)
    {
        ArgumentNullException.ThrowIfNull(relocation);
        this.Source = relocation.File.Asset;
        this.Relocated?.Invoke(this, new ModelRelocatedEventArgs(relocation));
    }

    private ModelImportSettings RequireRecord()
        => this.Record ?? throw new InvalidOperationException($"'{this.Source.Name}' has no import record.");

    // Records the user's part of the record, its settings and node continuity, so an undo keeps what an import rewrote.
    private void Change(ModelImportSettings next, string label)
    {
        var history = this.history ?? throw new InvalidOperationException("The model document has no history.");
        var previous = this.RequireRecord();
        this.Record = next;
        this.Changed?.Invoke(this, EventArgs.Empty);
        history.AddChange(label, () => this.Restore(previous.Settings, previous.NodeContinuity, label));
    }

    private void Restore(JsonElement settings, ImmutableArray<ModelNodeContinuity> continuity, string label)
        => this.Change(this.RequireRecord() with { Settings = settings, NodeContinuity = continuity }, label);
}
