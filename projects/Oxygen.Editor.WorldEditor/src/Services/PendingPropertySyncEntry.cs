// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// One buffered property sync request for a scene node.
/// </summary>
/// <param name="NodeId">The node id that receives the property values.</param>
/// <param name="Entries">The property values to replay.</param>
internal readonly record struct PendingPropertySyncEntry(
    Guid NodeId,
    IReadOnlyList<EnginePropertyValueEntry> Entries);
