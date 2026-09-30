// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads native-declared source inputs while retaining the observations capture must verify.</summary>
internal static partial class CookSavedSourceReader
{
    /// <summary>Reads native-declared inputs and retains their capture requirements.</summary>
    /// <param name="documents">Document owners protecting saved source bytes.</param>
    /// <param name="facts">The native source-analysis result.</param>
    /// <param name="root">The owning project root.</param>
    /// <param name="excluded">Operation-generated paths handled separately.</param>
    /// <param name="cancellationToken">Cancels source reads.</param>
    /// <returns>The discovered source inputs and their observations.</returns>
    internal static async Task<ImmutableArray<CookSnapshotInput>> ReadNativeInputsAsync(
        ICookDocumentRegistry documents,
        NativeSourceAnalysisJob facts,
        string root,
        IReadOnlySet<string> excluded,
        CancellationToken cancellationToken)
    {
        var declared = facts.Files.Select(static file => file.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var observed = facts.Observations.ToDictionary(static observation => observation.Path, StringComparer.OrdinalIgnoreCase);
        var inputs = ImmutableArray.CreateBuilder<CookSnapshotInput>();
        foreach (var path in observed.Keys.Concat(declared).Distinct(StringComparer.OrdinalIgnoreCase).Where(path => !excluded.Contains(path)))
        {
            var relative = Path.GetRelativePath(root, path).Replace('\\', '/');
            if (Path.IsPathRooted(relative) || relative is ".." || relative.StartsWith("../", StringComparison.Ordinal))
            {
                throw new InvalidDataException($"Source dependency '{path}' lies outside its source root '{root}'.");
            }

            _ = observed.TryGetValue(path, out var observation);
            var exists = observation?.Exists ?? Exists(path);
            var metadata = observation?.Metadata ?? (observation is null && exists ? ReadMetadata(path) : null);
            var kind = !exists ? CookSnapshotInputKind.Absent
                : metadata?.IsDirectory == true || (observation?.Reads.IsEmpty == true && !declared.Contains(path))
                    ? CookSnapshotInputKind.Probe : CookSnapshotInputKind.File;
            var hash = kind == CookSnapshotInputKind.File
                ? await HashAsync(documents, path, cancellationToken).ConfigureAwait(false) : string.Empty;
            inputs.Add(new(null, path, relative, hash, kind)
            {
                Metadata = metadata,
                NativeObservations = observation is null ? [] : [observation],
            });
        }

        return inputs.ToImmutable();
    }
}
