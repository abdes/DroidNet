// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Schemas;

/// <summary>
/// TimeMachine operand for a property edit. Carries the node set and the
/// before/after snapshots needed to execute or invert the operation.
/// </summary>
/// <param name="Nodes">The node ids edited by the operation. The list is
/// captured at session-begin time so undo cannot drift if selection
/// changes later.</param>
/// <param name="Before">The pre-edit snapshot.</param>
/// <param name="After">The post-edit snapshot.</param>
/// <param name="Label">A short, human-readable label for history UI.</param>
public sealed record PropertyOp(
    IReadOnlyList<Guid> Nodes,
    PropertySnapshot Before,
    PropertySnapshot After,
    string Label)
{
    /// <summary>
    /// Returns the inverse of this operation. Note that
    /// <c>op.Inverse().Inverse() == op</c> by construction.
    /// </summary>
    /// <returns>The inverse operation.</returns>
    public PropertyOp Inverse() => this with
    {
        Before = this.After,
        After = this.Before,
        Label = $"Undo {this.Label}",
    };

    /// <summary>
    /// Returns the per-node "after" edit, restricted to the property ids
    /// that actually changed between <see cref="Before"/> and <see cref="After"/>.
    /// </summary>
    /// <returns>The minimal effective edit.</returns>
    public IReadOnlyDictionary<Guid, PropertyEdit> EffectiveEdit()
    {
        var result = new Dictionary<Guid, PropertyEdit>();
        foreach (var nodeId in this.Nodes)
        {
            var beforeEdit = this.Before.PerNode.TryGetValue(nodeId, out var b) ? b : PropertyEdit.Empty;
            var afterEdit = this.After.PerNode.TryGetValue(nodeId, out var a) ? a : PropertyEdit.Empty;

            var diff = new PropertyEdit();
            foreach (var (id, afterValue) in afterEdit)
            {
                var changed = !beforeEdit.TryGetRaw(id, out var beforeValue) ||
                              !Equals(beforeValue, afterValue);
                if (changed)
                {
                    diff.SetRaw(id, afterValue);
                }
            }

            if (diff.Count > 0)
            {
                result[nodeId] = diff;
            }
        }

        return result;
    }

    /// <summary>
    /// Returns the union of property ids touched by either side of the
    /// operation across all nodes.
    /// </summary>
    /// <returns>The set of touched property ids.</returns>
    public IReadOnlySet<PropertyId> TouchedProperties()
    {
        var result = new HashSet<PropertyId>();
        foreach (var nodeId in this.Nodes)
        {
            if (this.After.PerNode.TryGetValue(nodeId, out var afterEdit))
            {
                foreach (var id in afterEdit.Ids)
                {
                    _ = result.Add(id);
                }
            }

            if (this.Before.PerNode.TryGetValue(nodeId, out var beforeEdit))
            {
                foreach (var id in beforeEdit.Ids)
                {
                    _ = result.Add(id);
                }
            }
        }

        return result;
    }
}
