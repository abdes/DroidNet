// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>
/// Finds the unsaved documents a cook reads from the references its saved descriptors make: a cook of an asset or a
/// folder reads the assets in it and everything they reference, and a cook of the project reads every asset.
/// </summary>
/// <param name="session">The project session, whose project root holds the documents a cook may read.</param>
/// <param name="documents">The open documents with unsaved changes.</param>
public sealed class UnsavedCookInputs(IProjectSession session, IUnsavedDocuments documents) : IUnsavedCookInputs
{
    /// <inheritdoc/>
    public async Task<IReadOnlyList<UnsavedDocument>> FindAsync(CookRunRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        var project = session.Context;

        // Most cooks meet no unsaved document, and need not read the references.
        var unsaved = documents.Find([project.ProjectRoot]);
        if (unsaved.Count == 0 || request.TargetKind == CookTargetKind.Project)
        {
            return unsaved;
        }

        var scope = request.TargetKind == CookTargetKind.Changes ? request.Assets
            : request.ScopeUri is { } uri ? (IReadOnlyList<AssetPath>)[uri] : [];
        var targets = scope.Select(AssetFileTypes.ToIdentity).ToArray();
        var index = await AuthoredReferenceIndex.BuildAsync(project, cancellationToken).ConfigureAwait(false);
        var read = ReadFiles(index, targets);
        return [.. unsaved.Where(document => read.Contains(document.File.Path)
            || targets.Any(AssetFileTypes.ToIdentity(document.File.Asset).IsSameOrInside))];
    }

    // The descriptors of the targets and of everything they reference, transitively.
    private static HashSet<FileSystemPath> ReadFiles(AuthoredReferenceIndex index, IReadOnlyList<AssetPath> targets)
    {
        var byIdentity = index.Descriptors.ToLookup(static descriptor => descriptor.Identity);
        var pending = new Queue<AuthoredReferenceIndex.Descriptor>(
            index.Descriptors.Where(descriptor => targets.Any(descriptor.Identity.IsSameOrInside)));
        var visited = new HashSet<AssetPath>();
        var files = new HashSet<FileSystemPath>();
        while (pending.TryDequeue(out var descriptor))
        {
            if (!visited.Add(descriptor.Identity))
            {
                continue;
            }

            _ = files.Add(descriptor.Location);
            foreach (var reference in descriptor.References)
            {
                foreach (var used in byIdentity[reference])
                {
                    pending.Enqueue(used);
                }
            }
        }

        return files;
    }
}
