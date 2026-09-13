// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Publishes immutable document state without performing I/O or scheduling cooks.</summary>
public sealed partial class CookDocumentRegistry
{
    private CookDocumentRegistrySnapshot CaptureState()
        => new(
            this.stateVersion,
            [
                .. this.registrations.Values.OrderBy(static registration => registration.Id)
                    .Select(static registration => registration.State).OfType<CookDocumentState>(),
            ]);

    private void UpdateState(long id, CookDocumentState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        state = state with { SourcePath = Path.GetFullPath(state.SourcePath) };
        CookDocumentStateChangedEventArgs change;
        lock (this.sync)
        {
            if (!this.registrations.TryGetValue(id, out var registration))
            {
                return;
            }

            if (!string.Equals(registration.SourcePath, Path.GetFullPath(state.SourcePath), StringComparison.OrdinalIgnoreCase)
                || (registration.State is { } owner && owner.DocumentId != state.DocumentId))
            {
                throw new ArgumentException("Document state must belong to this registered source and document.", nameof(state));
            }

            if (registration.State == state)
            {
                return;
            }

            var before = registration.State;
            registration.State = state;
            this.stateVersion++;
            change = new(before, state, this.CaptureState());
        }

        this.PublishState(change);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A presentation observer must not interrupt document edits or prevent other observers receiving state.")]
    private void PublishState(CookDocumentStateChangedEventArgs change)
    {
        foreach (var handler in this.StateChanged?.GetInvocationList() ?? [])
        {
            try
            {
                ((EventHandler<CookDocumentStateChangedEventArgs>)handler)(this, change);
            }
            catch (Exception exception)
            {
                this.LogStateObserverFailure(exception);
            }
        }
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "A document status subscriber failed.")]
    private partial void LogStateObserverFailure(Exception exception);
}
