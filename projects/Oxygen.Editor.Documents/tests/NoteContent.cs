// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.TimeMachine;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>The content of a <see cref="NoteKind"/> document.</summary>
/// <param name="identity">The asset the note names itself as.</param>
/// <param name="text">The note's text.</param>
/// <param name="references">The assets the note references.</param>
[ExcludeFromCodeCoverage]
internal sealed class NoteContent(AssetPath identity, string text, List<AssetPath> references) : IDocumentContent, IDisposable
{
    public AssetPath Identity { get; private set; } = identity;

    public string Text { get; private set; } = text;

    public IReadOnlyList<AssetPath> References => references;

    /// <summary>Gets or sets the history edits are recorded into, as an editor would get it from its document.</summary>
    public HistoryKeeper? History { get; set; }

    /// <summary>Gets or sets text being typed, which finishing edits records.</summary>
    public string? Pending { get; set; }

    /// <summary>Gets or sets why a save is refused, or null to allow it.</summary>
    public string? Refusal { get; set; }

    /// <summary>Gets or sets work an encode does before it returns, such as an edit made while the file is written.</summary>
    public Action? DuringEncode { get; set; }

    /// <summary>Gets or sets where the note keeps its text instead of its file, or null to write its file.</summary>
    public Func<string, CancellationToken, Task<DocumentSaveResult>>? StoreElsewhere { get; set; }

    public bool IsDisposed { get; private set; }

    public bool HasEditsInProgress => this.Pending is not null;

    public void Edit(string next)
    {
        var history = this.History ?? throw new InvalidOperationException("The note has no history.");
        var previous = this.Text;
        this.Text = next;
        history.AddChange($"Edit {next}", () => this.Edit(previous));
    }

    public Task FinishEditsAsync(CancellationToken cancellationToken)
    {
        if (this.Pending is { } pending)
        {
            this.Pending = null;
            this.Edit(pending);
        }

        return Task.CompletedTask;
    }

    public DocumentCapture Capture() => this.CaptureAs(this.Identity);

    public DocumentCapture CaptureCopy(DocumentFile copy) => this.CaptureAs(copy.Asset);

    public void ApplyRelocation(DocumentRelocation relocation)
    {
        this.Identity = relocation.File.Asset;
        for (var index = 0; index < references.Count; index++)
        {
            if (relocation.MapAsset(references[index]) is { } moved)
            {
                references[index] = moved;
            }
        }
    }

    public void Dispose() => this.IsDisposed = true;

    private DocumentCapture CaptureAs(AssetPath identity)
    {
        if (this.Refusal is { } refusal)
        {
            return DocumentCapture.Refused(refusal);
        }

        if (this.StoreElsewhere is { } store)
        {
            var text = this.Text;
            return DocumentCapture.Stored(token => store(text, token));
        }

        var bytes = NoteKind.Encode(identity, this.Text, [.. references]);
        return DocumentCapture.Of(async _ =>
        {
            await Task.Yield();
            this.DuringEncode?.Invoke();
            return bytes;
        });
    }
}
