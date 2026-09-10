// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Pure model+engine apply. The same function is used to "do" and to
/// "undo"; that's the structural property that makes
/// <c>redo(OP) == undo(UOP) == OP</c> hold by construction.
/// </summary>
public static class PropertyApply
{
    /// <summary>
    /// Applies the given side ("before" or "after") of an operation to
    /// every covered node.
    /// </summary>
    /// <param name="op">The property operation.</param>
    /// <param name="side">Which snapshot to apply.</param>
    /// <param name="resolver">Resolver for node id to model target and
    /// engine-sync function.</param>
    /// <param name="descriptors">The descriptors used to write values
    /// onto the model targets, indexed by property id.</param>
    /// <returns>A task that completes when all nodes are applied.</returns>
    public static async Task ApplyAsync(
        PropertyOp op,
        ApplySide side,
        IPropertyTarget resolver,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors)
    {
        ArgumentNullException.ThrowIfNull(op);
        ArgumentNullException.ThrowIfNull(resolver);
        ArgumentNullException.ThrowIfNull(descriptors);

        ApplyToTargets(op, side, resolver, descriptors);
        await PushToEngineAsync(op, side, resolver).ConfigureAwait(false);
    }

    /// <summary>Applies the complete model snapshot synchronously before engine I/O can yield.</summary>
    /// <param name="op">The property operation.</param>
    /// <param name="side">The snapshot to apply.</param>
    /// <param name="resolver">Resolves authoring targets.</param>
    /// <param name="descriptors">The property descriptors.</param>
    public static void ApplyToTargets(PropertyOp op, ApplySide side, IPropertyTarget resolver, IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors)
    {
        ArgumentNullException.ThrowIfNull(op);
        ArgumentNullException.ThrowIfNull(resolver);
        var snapshot = side == ApplySide.After ? op.After : op.Before;
        foreach (var nodeId in op.Nodes)
        {
            if (snapshot.PerNode.TryGetValue(nodeId, out var edit) && resolver.TryGetTarget(nodeId, out var target) && target is not null)
            {
                ApplyToTarget(target, edit, descriptors);
            }
        }
    }

    /// <summary>Synchronizes a previously applied model snapshot with the engine.</summary>
    /// <param name="op">The property operation.</param>
    /// <param name="side">The applied snapshot.</param>
    /// <param name="resolver">The engine synchronization boundary.</param>
    /// <returns>The synchronization task.</returns>
    public static async Task PushToEngineAsync(PropertyOp op, ApplySide side, IPropertyTarget resolver)
    {
        ArgumentNullException.ThrowIfNull(op);
        ArgumentNullException.ThrowIfNull(resolver);
        var snapshot = side == ApplySide.After ? op.After : op.Before;
        foreach (var nodeId in op.Nodes)
        {
            if (snapshot.PerNode.TryGetValue(nodeId, out var edit) && resolver.TryGetTarget(nodeId, out var target) && target is not null)
            {
                await resolver.PushToEngineAsync(nodeId, edit).ConfigureAwait(true);
            }
        }
    }

    /// <summary>
    /// Synchronously applies an edit to a single in-memory model target,
    /// without engine sync. Used by tests and by preview-only code paths
    /// that route engine sync separately.
    /// </summary>
    /// <param name="target">The model target.</param>
    /// <param name="edit">The edit.</param>
    /// <param name="descriptors">The descriptors used to write values.</param>
    public static void ApplyToTarget(
        object target,
        PropertyEdit edit,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(descriptors);

        foreach (var (id, value) in edit)
        {
            if (!descriptors.TryGetValue(id, out var descriptor))
            {
                throw new KeyNotFoundException(
                    $"No property descriptor is registered for '{id.Qualified()}'.");
            }

            descriptor.WriteBoxed(target, value);
        }
    }
}
