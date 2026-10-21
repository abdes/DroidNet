// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads native-declared source inputs while retaining the observations capture must verify.</summary>
internal static partial class CookSavedSourceReader
{
    /// <summary>Reads native-declared inputs and retains their capture requirements.</summary>
    /// <param name="facts">The native source-analysis result.</param>
    /// <param name="root">The owning project root.</param>
    /// <param name="excluded">Operation-generated paths handled separately.</param>
    /// <param name="cancellationToken">Cancels source reads.</param>
    /// <returns>The discovered source inputs and their observations.</returns>
    internal static async Task<ImmutableArray<CookSnapshotInput>> ReadNativeInputsAsync(
        NativeSourceAnalysisJob facts,
        FileSystemPath root,
        IReadOnlySet<FileSystemPath> excluded,
        CancellationToken cancellationToken)
    {
        var declared = facts.Files.Select(static file => FileSystemPath.From(file.Path)).ToHashSet();
        var observed = facts.Observations.ToDictionary(static observation => FileSystemPath.From(observation.Path));
        var inputs = ImmutableArray.CreateBuilder<CookSnapshotInput>();
        foreach (var path in observed.Keys.Concat(declared).Distinct().Where(path => !excluded.Contains(path)))
        {
            if (!path.IsSameOrInside(root))
            {
                throw new InvalidDataException($"Source dependency '{path}' lies outside its source root '{root}'.");
            }

            _ = observed.TryGetValue(path, out var observation);
            var exists = observation?.Exists ?? Exists(path.FullPath);
            var metadata = observation?.Metadata ?? (observation is null && exists ? ReadMetadata(path.FullPath) : null);
            var kind = !exists ? CookSnapshotInputKind.Absent
                : metadata?.IsDirectory == true || (observation?.Reads.IsEmpty == true && !declared.Contains(path))
                    ? CookSnapshotInputKind.Probe : CookSnapshotInputKind.File;
            var hash = kind == CookSnapshotInputKind.File
                ? await HashAsync(path.FullPath, cancellationToken).ConfigureAwait(false) : string.Empty;
            inputs.Add(new(AssetUri: null, path, path.GetPathBelow(root), hash, kind)
            {
                Metadata = metadata,
                NativeObservations = observation is null ? [] : [observation],
            });
        }

        return inputs.ToImmutable();
    }
}
