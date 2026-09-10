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
    /// <param name="target">The model target, or <see langword="null"/> when the node
    /// is unknown / removed.</param>
    /// <returns><see langword="true"/> when the node was found.</returns>
    public bool TryGetTarget(Guid nodeId, out object? target);

    /// <summary>
    /// Pushes the given edit to the engine for the given node.
    /// </summary>
    /// <param name="nodeId">The node id.</param>
    /// <param name="edit">The edit to apply.</param>
    /// <returns>A task that completes when the engine has accepted (or
    /// buffered) the edit.</returns>
    public Task PushToEngineAsync(Guid nodeId, PropertyEdit edit);
}
