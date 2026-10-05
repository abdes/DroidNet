// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
    /// Initializes a new instance of the <see cref="PropertySnapshot"/> class from a per-node edit dictionary.
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
