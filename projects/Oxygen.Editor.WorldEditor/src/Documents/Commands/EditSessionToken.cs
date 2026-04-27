// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Groups inspector sub-edits into one command session.
/// </summary>
public sealed class EditSessionToken
{
    private EditSessionToken(
        Guid sessionId,
        string operationKind,
        IReadOnlyList<Guid> nodeIds,
        string fieldKey,
        bool isOneShot)
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
}

/// <summary>
/// State of an inspector edit session.
/// </summary>
public enum EditSessionState
{
    /// <summary>
    /// The session is open.
    /// </summary>
    Open,

    /// <summary>
    /// The session was committed.
    /// </summary>
    Committed,

    /// <summary>
    /// The session was cancelled.
    /// </summary>
    Cancelled,
}
