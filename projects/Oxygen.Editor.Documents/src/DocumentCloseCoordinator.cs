// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using Microsoft.UI;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Documents;

/// <summary>Coordinates save decisions before any editor authoring state is released.</summary>
/// <param name="prompt">The application-owned close dialog.</param>
/// <param name="operationResults">The visible operation-result publisher.</param>
public sealed partial class DocumentCloseCoordinator(IDocumentClosePrompt prompt, IOperationResultPublisher operationResults)
{
    private readonly Dictionary<(WindowId window, Guid document), IDocumentCloseParticipant> participants = [];

    /// <summary>Registers the authoring owner of an open document.</summary>
    /// <param name="windowId">The owner window.</param>
    /// <param name="documentId">The tab identity, which may differ from the authoring-service identity.</param>
    /// <param name="participant">The authoring owner.</param>
    public void Register(WindowId windowId, Guid documentId, IDocumentCloseParticipant participant)
        => this.participants.Add((windowId, documentId), participant);

    /// <summary>Unregisters a closed document.</summary>
    /// <param name="windowId">The owner window.</param>
    /// <param name="documentId">The tab identity.</param>
    public void Unregister(WindowId windowId, Guid documentId)
        => this.participants.Remove((windowId, documentId));

    /// <summary>Prepares authoring owners and collects decisions without destroying documents.</summary>
    /// <param name="windowId">The owner window.</param>
    /// <param name="documents">The documents being closed.</param>
    /// <param name="isWorkspaceClose">Whether to use the combined dialog.</param>
    /// <param name="force">Whether the caller explicitly requested a forced discard.</param>
    /// <returns>The prepared transaction, or null on cancellation or failure.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The close boundary must preserve documents and publish failures from authoring and dialog providers.")]
    internal async Task<DocumentCloseTransaction?> PrepareAsync(
        WindowId windowId,
        IReadOnlyList<IDocumentMetadata> documents,
        bool isWorkspaceClose,
        bool force)
    {
        var prepared = new List<ParticipantEntry>();
        var retained = false;
        try
        {
            await this.PrepareParticipantsAsync(windowId, documents, prepared).ConfigureAwait(true);
            var dirty = prepared.Where(entry => entry.Metadata.IsDirty)
                .Select(entry => new DocumentCloseItem(entry.Metadata, () => this.SaveAsync(entry), entry.Participant as IDocumentConflictParticipant))
                .ToArray();
            if (!force && dirty.Length > 0
                && !await prompt.ConfirmAsync(windowId, dirty, isWorkspaceClose).ConfigureAwait(true))
            {
                return null;
            }

            if (!force && dirty.Any(item => item.IsSelected && !item.IsSaved))
            {
                return null;
            }

            var discards = dirty.Where(item => force || !item.IsSelected)
                .Select(item => item.Metadata.DocumentId).ToHashSet();
            retained = true;
            return new DocumentCloseTransaction(
                () => this.CommitAsync(prepared, discards),
                () => ResumeEditing(prepared));
        }
        catch (OperationCanceledException)
        {
            return null;
        }
        catch (Exception ex)
        {
            this.PublishFailure(prepared.LastOrDefault()?.Metadata, "Document.Close", ex.Message);
            return null;
        }
        finally
        {
            if (!retained)
            {
                ResumeEditing(prepared);
            }
        }
    }

    private static void ResumeEditing(IEnumerable<ParticipantEntry> prepared)
    {
        foreach (var entry in prepared)
        {
            entry.Participant.ResumeEditing();
        }
    }

    private async Task PrepareParticipantsAsync(
        WindowId windowId, IReadOnlyList<IDocumentMetadata> documents, List<ParticipantEntry> prepared)
    {
        foreach (var metadata in documents)
        {
            if (!this.participants.TryGetValue((windowId, metadata.DocumentId), out var participant))
            {
                if (metadata.IsDirty)
                {
                    throw new InvalidOperationException($"The editor for '{metadata.Title}' is unavailable. Its unsaved changes cannot be safely closed.");
                }

                continue;
            }

            prepared.Add(new ParticipantEntry(metadata, participant));
            await participant.PrepareForCloseAsync().ConfigureAwait(true);
        }
    }

    private async Task<bool> CommitAsync(IReadOnlyList<ParticipantEntry> prepared, HashSet<Guid> discards)
    {
        var changed = prepared.FirstOrDefault(entry => entry.Metadata.IsDirty && !discards.Contains(entry.Metadata.DocumentId));
        if (changed is not null)
        {
            const string message = "A document changed after the close decision. Review its changes before closing again.";
            this.PublishFailure(changed.Metadata, "Document.Close", message);
            return false;
        }

        foreach (var entry in prepared)
        {
            await entry.Participant.CloseAsync(discards.Contains(entry.Metadata.DocumentId)).ConfigureAwait(true);
        }

        return true;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Save provider exceptions must become a visible failed save while the dialog and document remain open.")]
    private async Task<bool> SaveAsync(ParticipantEntry entry)
    {
        try
        {
            var version = (entry.Metadata as BaseDocumentMetadata)?.ChangeVersion;
            var saved = await entry.Participant.SaveForCloseAsync().ConfigureAwait(true);
            if (entry.Metadata is BaseDocumentMetadata metadata && metadata.ChangeVersion != version)
            {
                metadata.IsDirty = true;
                this.PublishFailure(metadata, "Document.Save", "The document changed while saving. Save again before closing.");
                return false;
            }

            return saved && !entry.Metadata.IsDirty;
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception ex)
        {
            this.PublishFailure(entry.Metadata, "Document.Save", ex.Message);
            return false;
        }
    }

    private void PublishFailure(IDocumentMetadata? metadata, string operationKind, string message)
    {
        var operationId = Guid.NewGuid();
        var scope = new AffectedScope { DocumentId = metadata?.DocumentId, DocumentName = metadata?.Title };
        operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = "Document kept open",
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = scope,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.Document,
                    Severity = DiagnosticSeverity.Error,
                    Code = DiagnosticCodes.DocumentPrefix + "CLOSE_FAILED",
                    Message = message,
                    AffectedEntity = scope,
                },
            ],
        });
    }

    private sealed partial record ParticipantEntry(IDocumentMetadata Metadata, IDocumentCloseParticipant Participant);
}
