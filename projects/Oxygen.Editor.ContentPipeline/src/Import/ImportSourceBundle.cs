// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>A complete source layout and the hashes used to discover it.</summary>
/// <param name="PrimaryRelativePath">The primary file relative to this bundle's common root.</param>
/// <param name="Files">The original files and their contained bundle destinations.</param>
public sealed record ImportSourceBundle(string PrimaryRelativePath, ImmutableArray<CookSnapshotInput> Files);
