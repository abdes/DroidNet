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
    /// <summary>
    /// Finds the closest folder that holds <paramref name="folder"/> and every existing file the
    /// analysis names on the same filesystem root, so parent-relative inputs keep their layout.
    /// </summary>
    /// <param name="facts">The native source-analysis result.</param>
    /// <param name="folder">The primary source's folder.</param>
    /// <returns>The folder the source's inputs are kept under.</returns>
    internal static FileSystemPath FindNativeInputsRoot(NativeSourceAnalysisJob facts, FileSystemPath folder)
    {
        var root = folder;
        foreach (var path in NamedPaths(facts).Where(static named => named.Exists).Select(static named => named.Path))
        {
            var candidate = root;
            while (!path.IsSameOrInside(candidate) && candidate.Parent is { } parent)
            {
                candidate = parent;
            }

            if (path.IsSameOrInside(candidate))
            {
                root = candidate;
            }
        }

        return root;
    }

    /// <summary>
    /// Reads native-declared inputs under <paramref name="root"/> and retains their capture requirements.
    /// A missing file outside the root, such as a DCC's temporary folder, is omitted: no bundle can hold
    /// it and the import proceeds without it. An existing file outside the root cannot be kept and is
    /// returned for the caller to refuse.
    /// </summary>
    /// <param name="facts">The native source-analysis result.</param>
    /// <param name="root">The folder the source's inputs are kept under.</param>
    /// <param name="excluded">Operation-generated paths handled separately.</param>
    /// <param name="cancellationToken">Cancels source reads.</param>
    /// <returns>The source inputs with their observations, and the paths outside the root.</returns>
    internal static async Task<NativeSourceInputs> ReadNativeInputsAsync(
        NativeSourceAnalysisJob facts,
        FileSystemPath root,
        IReadOnlySet<FileSystemPath> excluded,
        CancellationToken cancellationToken)
    {
        var declared = facts.Files.Select(static file => FileSystemPath.From(file.Path)).ToHashSet();
        var inputs = ImmutableArray.CreateBuilder<CookSnapshotInput>();
        var omitted = ImmutableArray.CreateBuilder<FileSystemPath>();
        var outside = ImmutableArray.CreateBuilder<FileSystemPath>();
        foreach (var (path, observation, exists) in NamedPaths(facts).Where(named => !excluded.Contains(named.Path)))
        {
            if (!path.IsSameOrInside(root))
            {
                (exists ? outside : omitted).Add(path);
                continue;
            }

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

        return new(inputs.ToImmutable(), omitted.ToImmutable(), outside.ToImmutable());
    }

    // Every path the analysis observed or declared, once, with its observation and whether it exists.
    private static IEnumerable<(FileSystemPath Path, NativeSourceObservation? Observation, bool Exists)> NamedPaths(NativeSourceAnalysisJob facts)
    {
        var observed = facts.Observations.ToDictionary(static observation => FileSystemPath.From(observation.Path));
        foreach (var path in observed.Keys.Concat(facts.Files.Select(static file => FileSystemPath.From(file.Path))).Distinct())
        {
            _ = observed.TryGetValue(path, out var observation);
            yield return (path, observation, observation?.Exists ?? Exists(path.FullPath));
        }
    }
}
