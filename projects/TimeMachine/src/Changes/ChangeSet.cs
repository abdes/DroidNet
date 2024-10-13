// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine.Changes;

public class ChangeSet : Change
{
    private readonly IList<IChange> changes = [];

    public IEnumerable<IChange> Changes => this.changes;

    /// <summary>
    /// Undo all Changes in this ChangeSet.
    /// </summary>
    public override void Apply()
    {
        foreach (var change in this.Changes)
        {
            change.Apply();
        }
    }

    internal void Add(IChange change) => this.changes.Add(change);
}
