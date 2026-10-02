// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>One source dependency discovered before coherent capture.</summary>
/// <param name="AssetUri">The logical authored identity, when this file is an asset.</param>
/// <param name="SourcePath">The original absolute source path.</param>
/// <param name="RelativePath">The preserved path inside the private input root.</param>
/// <param name="DiscoveryHash">The discovered content hash for a file; empty for probes and absent paths.</param>
/// <param name="Kind">Whether capture retains bytes, a positive probe or absence.</param>
public sealed record CookSnapshotInput(Uri? AssetUri, string SourcePath, string RelativePath, string DiscoveryHash, CookSnapshotInputKind Kind = CookSnapshotInputKind.File)
{
    /// <summary>Gets a value indicating whether discovery observed an absent path.</summary>
    [System.Text.Json.Serialization.JsonIgnore]
    public bool IsAbsent => this.Kind == CookSnapshotInputKind.Absent;

    /// <summary>Gets a value indicating whether the source root itself was observed as a directory.</summary>
    [System.Text.Json.Serialization.JsonIgnore]
    public bool IsRootDirectoryProbe => string.Equals(this.RelativePath, ".", StringComparison.Ordinal) && this.Kind == CookSnapshotInputKind.Probe && this.Metadata?.IsDirectory == true;

    /// <summary>Gets source metadata: observed fields for a probe, or original file metadata after capture.</summary>
    public Import.NativeSourceFileMetadata? Metadata { get; init; }

    /// <summary>Gets native discovery observations that must match the captured source.</summary>
    [System.Text.Json.Serialization.JsonIgnore]
    public ImmutableArray<Import.NativeSourceObservation> NativeObservations { get; init; } = [];

    /// <summary>Gets the decision-relevant metadata of a presence probe.</summary>
    internal Import.NativeSourceShape? ProbeShape => this.Kind == CookSnapshotInputKind.Probe ? this.Metadata?.Shape : null;
}
