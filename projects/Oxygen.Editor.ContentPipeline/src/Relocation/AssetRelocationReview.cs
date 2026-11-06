// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>What the user reviews before a relocation that reaches a shared mount.</summary>
/// <param name="ClosureMoves">What a move into a shared mount also moves from the project's own mounts.</param>
/// <param name="Referrers">The authored files that reference a moved asset and stay in place.</param>
/// <param name="SharedMounts">The shared mounts the relocation changes, whose other projects it does not update.</param>
/// <param name="ReferencedSources">The sources in other mounts that models moved into a shared mount reference.</param>
public sealed record AssetRelocationReview(
    IReadOnlyList<AssetRelocationMove> ClosureMoves,
    IReadOnlyList<AssetPath> Referrers,
    IReadOnlyList<string> SharedMounts,
    IReadOnlyList<AssetPath> ReferencedSources);
