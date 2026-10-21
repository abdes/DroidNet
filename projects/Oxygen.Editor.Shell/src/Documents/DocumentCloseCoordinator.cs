// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using Microsoft.UI;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// Makes the close decisions of one window's documents: asks about unsaved documents, saves the chosen ones, and closes
/// their editors, before any editor state is released.
/// </summary>
/// <param name="prompt">Asks the window's user about unsaved documents.</param>
/// <param name="operationResults">Publishes the failures that keep a document open.</param>
/// <param name="documents">The document service, which resolves a document whose file changed on disk.</param>
public sealed class DocumentCloseCoordinator(IDocumentClosePrompt prompt, IOperationResultPublisher operationResults, IDocumentService? documents = null)
{
    private readonly Dictionary<DocumentId, ParticipantEntry> participants = [];

    /// <summary>Registers the editor of a document shown in the window.</summary>
    /// <param name="tab">The document's tab.</param>
    /// <param name="participant">The editor.</param>
    public void Register(DocumentTab tab, IDocumentCloseParticipant participant)
    {
        ArgumentNullException.ThrowIfNull(tab);
        ArgumentNullException.ThrowIfNull(participant);
        this.participants.Add(tab.DocumentId, new ParticipantEntry(tab, participant));
    }

    /// <summary>Unregisters the editor of a closed document.</summary>
    /// <param name="tab">The document's tab.</param>
    public void Unregister(DocumentTab tab)
    {
        ArgumentNullException.ThrowIfNull(tab);
        _ = this.participants.Remove(tab.DocumentId);
    }

    /// <summary>Prepares authoring owners and collects decisions without destroying documents.</summary>
    /// <param name="window">The window whose tabs close, which asks about their unsaved changes.</param>
    /// <param name="tabs">The tabs being closed.</param>
    /// <param name="force">Whether the caller explicitly requested a forced discard.</param>
    /// <returns>The prepared transaction, or null on cancellation or failure.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The close boundary must preserve documents and publish failures from authoring and dialog providers.")]
    internal async Task<DocumentCloseTransaction?> PrepareAsync(WindowId window, IReadOnlyList<IDocumentMetadata> tabs, bool force)
    {
        var prepared = new List<ParticipantEntry>();
        var retained = false;
        try
        {
            await this.PrepareParticipantsAsync(tabs, prepared).ConfigureAwait(true);
            var dirty = prepared.Where(entry => entry.Tab.IsDirty)
                .Select(entry => new DocumentCloseItem(entry.Tab, () => this.SaveAsync(entry), documents))
                .ToArray();
            if (!force && dirty.Length > 0
                && !await prompt.ConfirmAsync(window, dirty).ConfigureAwait(true))
            {
                return null;
            }

            if (!force && dirty.Any(item => item.IsSelected && !item.IsSaved))
            {
                return null;
            }

            var discards = dirty.Where(item => force || !item.IsSelected)
                .Select(item => item.Tab.DocumentId).ToHashSet();
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
            this.PublishFailure(prepared.LastOrDefault()?.Tab.Title, "Document.Close", ex.Message);
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

    private async Task PrepareParticipantsAsync(IReadOnlyList<IDocumentMetadata> tabs, List<ParticipantEntry> prepared)
    {
        foreach (var tab in tabs)
        {
            if (!this.participants.TryGetValue(tab.DocumentId, out var entry))
            {
                if (tab.IsDirty)
                {
                    throw new InvalidOperationException($"The editor for '{tab.Title}' is unavailable. Its unsaved changes cannot be safely closed.");
                }

                continue;
            }

            prepared.Add(entry);
            await entry.Participant.PrepareForCloseAsync().ConfigureAwait(true);
        }
    }

    private async Task<bool> CommitAsync(IReadOnlyList<ParticipantEntry> prepared, HashSet<DocumentId> discards)
    {
        var changed = prepared.FirstOrDefault(entry => entry.Tab.IsDirty && !discards.Contains(entry.Tab.DocumentId));
        if (changed is not null)
        {
            const string message = "A document changed after the close decision. Review its changes before closing again.";
            this.PublishFailure(changed.Tab.Title, "Document.Close", message);
            return false;
        }

        foreach (var entry in prepared)
        {
            await entry.Participant.CloseAsync(discards.Contains(entry.Tab.DocumentId)).ConfigureAwait(true);
        }

        return true;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Save provider exceptions must become a visible failed save while the dialog and document remain open.")]
    private async Task<bool> SaveAsync(ParticipantEntry entry)
    {
        try
        {
            // A save marks saved only what it captured, so an edit made while it wrote keeps the document dirty.
            var saved = await entry.Participant.SaveForCloseAsync().ConfigureAwait(true);
            if (saved && entry.Tab.IsDirty)
            {
                this.PublishFailure(entry.Tab.Title, "Document.Save", "The document changed while saving. Save again before closing.");
                return false;
            }

            return saved;
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception ex)
        {
            this.PublishFailure(entry.Tab.Title, "Document.Save", ex.Message);
            return false;
        }
    }

    private void PublishFailure(string? title, string operationKind, string message)
    {
        var operationId = Guid.NewGuid();
        operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = title is null ? "Document kept open" : $"{title} kept open",
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.Document,
                    Severity = DiagnosticSeverity.Error,
                    Code = DiagnosticCodes.DocumentPrefix + "CLOSE_FAILED",
                    Message = message,
                },
            ],
        });
    }

    private sealed record ParticipantEntry(DocumentTab Tab, IDocumentCloseParticipant Participant);
}
