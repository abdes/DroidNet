// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// One buffered property sync request for a scene node.
/// </summary>
/// <param name="RequestId">The identity acknowledged only after successful replay.</param>
/// <param name="Revision">The originating authoring lifetime and order.</param>
/// <param name="NodeId">The node id that receives the property values.</param>
/// <param name="Entries">The property values to replay.</param>
/// <param name="Failure">The most recent unsuccessful replay outcome.</param>
internal sealed record PendingPropertySyncEntry(
    Guid RequestId,
    SceneSyncRevision Revision,
    Guid NodeId,
    IReadOnlyList<EnginePropertyValueEntry> Entries,
    SyncOutcome? Failure = null);
