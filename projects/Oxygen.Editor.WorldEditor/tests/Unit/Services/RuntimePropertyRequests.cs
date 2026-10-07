// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Services;

/// <summary>
/// Reads the property values dispatched for one node and component, independent of how many
/// property commands carried them or which other components were projected alongside.
/// </summary>
internal static class RuntimePropertyRequests
{
    /// <summary>Gets the property entries sent to one component of one node, in dispatch order.</summary>
    /// <param name="requests">The captured runtime requests.</param>
    /// <param name="nodeId">The target node.</param>
    /// <param name="component">The target component.</param>
    /// <returns>The dispatched entries.</returns>
    public static IEnumerable<RuntimePropertyValue> PropertyEntries(
        this IEnumerable<RuntimeWorldRequest> requests,
        Guid nodeId,
        EngineComponentId component)
        => requests
            .Select(static request => request.Command)
            .OfType<RuntimeSetProperties>()
            .Where(command => command.NodeId == nodeId)
            .SelectMany(static command => command.Entries)
            .Where(entry => entry.ComponentId == (ushort)component);

    /// <summary>Builds the wire value of a transform X-position edit.</summary>
    /// <param name="value">The position in meters.</param>
    /// <returns>The expected entry.</returns>
    public static RuntimePropertyValue PositionX(float value)
        => new((ushort)EngineComponentId.Transform, (ushort)TransformField.PositionX, value);
}
