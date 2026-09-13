// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Live document presentation facts; saved-input capture still acquires the document read gates.</summary>
/// <param name="Version">The monotonic registry-state version.</param>
/// <param name="Documents">The last published state of each current document owner.</param>
public sealed record CookDocumentRegistrySnapshot(long Version, ImmutableArray<CookDocumentState> Documents);
