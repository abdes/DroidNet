// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Groups inspector sub-edits into one command session.
/// </summary>
public sealed class EditSessionToken
{
    private readonly CompletionState completion;

    private EditSessionToken(
        Guid sessionId,
        string operationKind,
        IReadOnlyList<Guid> nodeIds,
        string fieldKey,
        bool isOneShot,
        CompletionState? completion = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(operationKind);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentException.ThrowIfNullOrWhiteSpace(fieldKey);

        this.SessionId = sessionId;
        this.OperationKind = operationKind;
        this.NodeIds = [.. nodeIds];
        this.FieldKey = fieldKey;
        this.IsOneShot = isOneShot;
        this.State = isOneShot ? EditSessionState.Committed : EditSessionState.Open;
        this.completion = completion ?? new();
    }

    /// <summary>
    /// Gets the one-shot session used by menu/button commits.
    /// </summary>
    public static EditSessionToken OneShot { get; } = new(
        Guid.Empty,
        "OneShot",
        [],
        "OneShot",
        isOneShot: true);

    /// <summary>
    /// Gets the session identity.
    /// </summary>
    public Guid SessionId { get; }

    /// <summary>
    /// Gets the operation kind associated with this edit session.
    /// </summary>
    public string OperationKind { get; }

    /// <summary>
    /// Gets the edited node identities.
    /// </summary>
    public IReadOnlyList<Guid> NodeIds { get; }

    /// <summary>
    /// Gets the edited field key.
    /// </summary>
    public string FieldKey { get; }

    /// <summary>
    /// Gets a value indicating whether this token represents a one-shot commit.
    /// </summary>
    public bool IsOneShot { get; }

    /// <summary>
    /// Gets the session state.
    /// </summary>
    public EditSessionState State { get; private set; }

    /// <summary>Gets a value indicating whether the command owner already closed this gesture.</summary>
    internal bool IsConsumed => this.completion.Consumed;

    /// <summary>
    /// Starts an interactive edit session.
    /// </summary>
    /// <param name="operationKind">The operation kind.</param>
    /// <param name="nodeIds">The edited node identities.</param>
    /// <param name="fieldKey">The edited field key.</param>
    /// <returns>The edit session token.</returns>
    public static EditSessionToken Begin(
        string operationKind,
        IReadOnlyList<Guid> nodeIds,
        string fieldKey)
        => new(Guid.NewGuid(), operationKind, nodeIds, fieldKey, isOneShot: false);

    /// <summary>Captures the phase before an asynchronous producer can observe a later commit or cancellation.</summary>
    /// <returns>A token with the same identity and the current immutable request phase.</returns>
    public EditSessionToken Capture()
        => this.IsOneShot ? this : new(this.SessionId, this.OperationKind, this.NodeIds, this.FieldKey, isOneShot: false, this.completion) { State = this.State };

    /// <summary>
    /// Marks the session as committed.
    /// </summary>
    public void Commit()
    {
        if (this.IsOneShot)
        {
            return;
        }

        this.State = EditSessionState.Committed;
    }

    /// <summary>
    /// Marks the session as cancelled.
    /// </summary>
    public void Cancel()
    {
        if (this.IsOneShot)
        {
            return;
        }

        this.State = EditSessionState.Cancelled;
    }

    /// <summary>Prevents late control callbacks from reopening a gesture completed by Save or Close.</summary>
    internal void Consume()
    {
        if (!this.IsOneShot)
        {
            this.completion.Consumed = true;
        }
    }

    private sealed class CompletionState
    {
        public bool Consumed { get; set; }
    }
}
