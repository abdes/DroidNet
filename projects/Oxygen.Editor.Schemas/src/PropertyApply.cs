// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Opaque handle that resolves a node id to its mutable model target and
/// to an asynchronous engine-sync function.
/// </summary>
/// <remarks>
/// The schema layer cannot reference the world / scene / interop types
/// directly. Callers (the command service) inject this resolver, so the
/// schema layer remains pure.
/// </remarks>
public interface IPropertyTarget
{
    /// <summary>
    /// Tries to obtain the model target object that owns the property
    /// values for the given node id.
    /// </summary>
    /// <param name="nodeId">The node id.</param>
    /// <param name="target">The model target, or <c>null</c> when the node
    /// is unknown / removed.</param>
    /// <returns><see langword="true"/> when the node was found.</returns>
    bool TryGetTarget(Guid nodeId, out object? target);

    /// <summary>
    /// Pushes the given edit to the engine for the given node.
    /// </summary>
    /// <param name="nodeId">The node id.</param>
    /// <param name="edit">The edit to apply.</param>
    /// <returns>A task that completes when the engine has accepted (or
    /// buffered) the edit.</returns>
    Task PushToEngineAsync(Guid nodeId, PropertyEdit edit);
}

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

        var snapshot = side == ApplySide.After ? op.After : op.Before;

        foreach (var nodeId in op.Nodes)
        {
            if (!snapshot.PerNode.TryGetValue(nodeId, out var edit))
            {
                continue;
            }

            if (!resolver.TryGetTarget(nodeId, out var target) || target is null)
            {
                continue;
            }

            // 1. Update the model — the source of truth for the editor.
            ApplyToTarget(target, edit, descriptors);

            // 2. Push to engine. Single round-trip per node carrying every
            //    changed property, in line with §5.3.
            await resolver.PushToEngineAsync(nodeId, edit).ConfigureAwait(false);
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

/// <summary>
/// Selects which side of a <see cref="PropertyOp"/> to apply.
/// </summary>
public enum ApplySide
{
    /// <summary>Apply the pre-edit snapshot (used by undo).</summary>
    Before,

    /// <summary>Apply the post-edit snapshot (used by do/redo).</summary>
    After,
}
