// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Saved authoring dependencies discovered before coherent capture.</summary>
/// <param name="Assets">The requested assets and their authored dependencies.</param>
/// <param name="Files">Source, settings and media files with discovery hashes.</param>
/// <param name="Dependencies">Direct asset dependencies, keyed by authored source identity.</param>
/// <param name="FileDependencies">Saved file paths that directly affect each product's emitted data.</param>
/// <param name="Builtins">Engine-provided identities whose recipes are supplied by the engine catalog.</param>
/// <param name="PublishedReferences">Cooked-only references requiring published-output validation and leases.</param>
/// <param name="Diagnostics">Input problems collected across independent assets; errors prevent capture and cooking.</param>
public sealed record CookDependencyGraph(
    ImmutableArray<ContentCookInput> Assets,
    ImmutableArray<CookSnapshotInput> Files,
    ImmutableDictionary<Uri, ImmutableArray<Uri>> Dependencies,
    ImmutableDictionary<Uri, ImmutableArray<string>> FileDependencies,
    ImmutableArray<Uri> Builtins,
    ImmutableArray<Uri> PublishedReferences,
    ImmutableArray<DiagnosticRecord> Diagnostics)
{
    /// <summary>Gets source-revision dependency facts retained with successful imported products.</summary>
    public ImmutableDictionary<Uri, Import.ImportedSourceDependencyState> ImportedSources { get; init; } = ImmutableDictionary<Uri, Import.ImportedSourceDependencyState>.Empty;
}
