// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

/// <summary>
/// Represents a collection of changes that can be applied as a single unit.
/// </summary>
/// <remarks>
/// The <see cref="ChangeSet"/> class allows multiple changes to be grouped together and applied in sequence.
/// This is useful for scenarios where a series of changes need to be treated as a single atomic operation
/// within the undo/redo system.
/// </remarks>
public class ChangeSet : Change
{
    private readonly IList<IChange> changes = [];

    /// <summary>
    /// Gets the collection of changes in this <see cref="ChangeSet"/>.
    /// </summary>
    /// <value>
    /// An enumerable collection of changes.
    /// </value>
    public IEnumerable<IChange> Changes => this.changes;

    /// <summary>
    /// Applies all changes in this <see cref="ChangeSet"/>.
    /// </summary>
    /// <remarks>
    /// This method iterates through all changes in the set and applies each one in sequence.
    /// </remarks>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// var changeSet = new ChangeSet { Key = "exampleChangeSet" };
    /// changeSet.Add(new CustomChange { Key = "change1" });
    /// changeSet.Add(new CustomChange { Key = "change2" });
    /// changeSet.Apply();
    /// ]]></code>
    /// </example>
    public override void Apply()
    {
        foreach (var change in this.Changes)
        {
            change.Apply();
        }
    }

    /// <inheritdoc />
    public override async ValueTask ApplyAsync(CancellationToken cancellationToken = default)
    {
        foreach (var change in this.Changes)
        {
            cancellationToken.ThrowIfCancellationRequested();
            await change.ApplyAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Adds a change to this <see cref="ChangeSet"/>.
    /// </summary>
    /// <param name="change">The change to add.</param>
    /// <remarks>
    /// Changes are added to the beginning of the list, ensuring that the most recent changes are applied first.
    /// </remarks>
    internal void Add(IChange change) => this.changes.Insert(0, change);
}
