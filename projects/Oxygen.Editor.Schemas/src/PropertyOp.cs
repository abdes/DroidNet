// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Linq;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// A snapshot of property state for one or more nodes, used as the
/// before/after operand in <see cref="PropertyOp"/>.
/// </summary>
/// <remarks>
/// Snapshots are immutable. The keys are stable across edits because the
/// engine identifies nodes by GUID; the values are boxed property
/// values.
/// </remarks>
public sealed class PropertySnapshot
{
    private readonly Dictionary<Guid, PropertyEdit> perNode;

    /// <summary>
    /// Initializes a new snapshot from a per-node edit dictionary.
    /// </summary>
    /// <param name="perNode">Map from node id to the edit that captures
    /// that node's relevant property values.</param>
    public PropertySnapshot(IReadOnlyDictionary<Guid, PropertyEdit> perNode)
    {
        ArgumentNullException.ThrowIfNull(perNode);
        this.perNode = new Dictionary<Guid, PropertyEdit>(perNode.Count);
        foreach (var (k, v) in perNode)
        {
            this.perNode[k] = v.Clone();
        }
    }

    /// <summary>
    /// Gets the node ids covered by the snapshot.
    /// </summary>
    public IReadOnlyCollection<Guid> Nodes => this.perNode.Keys;

    /// <summary>
    /// Gets the per-node edit map.
    /// </summary>
    public IReadOnlyDictionary<Guid, PropertyEdit> PerNode => this.perNode;

    /// <summary>
    /// Builds a snapshot by reading the listed properties off the
    /// resolved model objects.
    /// </summary>
    /// <param name="nodeTargets">Map from node id to the model object
    /// that owns the property values (e.g. the C# transform component
    /// instance).</param>
    /// <param name="descriptors">The descriptors to capture.</param>
    /// <returns>The new snapshot.</returns>
    public static PropertySnapshot Capture(
        IReadOnlyDictionary<Guid, object> nodeTargets,
        IReadOnlyList<PropertyDescriptor> descriptors)
    {
        ArgumentNullException.ThrowIfNull(nodeTargets);
        ArgumentNullException.ThrowIfNull(descriptors);

        var perNode = new Dictionary<Guid, PropertyEdit>(nodeTargets.Count);
        foreach (var (id, target) in nodeTargets)
        {
            var edit = new PropertyEdit();
            foreach (var descriptor in descriptors)
            {
                edit.SetRaw(descriptor.Id, descriptor.ReadBoxed(target));
            }

            perNode[id] = edit;
        }

        return new PropertySnapshot(perNode);
    }

    /// <summary>
    /// Returns a snapshot containing only the listed nodes.
    /// </summary>
    /// <param name="nodeIds">The node ids to keep.</param>
    /// <returns>The filtered snapshot.</returns>
    public PropertySnapshot KeepNodes(IEnumerable<Guid> nodeIds)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        var subset = new Dictionary<Guid, PropertyEdit>();
        foreach (var id in nodeIds)
        {
            if (this.perNode.TryGetValue(id, out var edit))
            {
                subset[id] = edit;
            }
        }

        return new PropertySnapshot(subset);
    }
}

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
