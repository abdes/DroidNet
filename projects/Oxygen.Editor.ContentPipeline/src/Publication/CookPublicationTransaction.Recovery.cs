// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Restores complete prior roots without deleting unrecognized content.</summary>
internal sealed partial class CookPublicationTransaction
{
    /// <summary>Loads recovery metadata only for the held project's operation directory.</summary>
    /// <param name="project">The project being reopened.</param>
    /// <param name="operationId">The operation directory to inspect.</param>
    /// <param name="files">The atomic metadata store.</param>
    /// <param name="writer">The exclusive recovery owner.</param>
    /// <param name="cancellationToken">Cancels loading before restoration starts.</param>
    /// <returns>The validated journal owner.</returns>
    public static async Task<CookPublicationTransaction> LoadAsync(ProjectContext project, Guid operationId, IAtomicFileStore files, CookOutputWriteLease writer, CancellationToken cancellationToken)
    {
        writer.VerifyOwner(project.ProjectRoot);
        var directory = Path.Combine(project.ProjectRoot, ".build", "cook", operationId.ToString("N"));
        CookOutputLease.RejectReparsePoint(directory);
        var path = Path.Combine(directory, "publication.json");
        CookOutputLease.RejectReparsePoint(path);
        var snapshot = await files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        if (!snapshot.Version.Exists)
        {
            throw new InvalidDataException("The publication recovery journal is missing.");
        }

        var loadedJournal = JsonSerializer.Deserialize<CookPublicationJournal>(snapshot.Content.AsSpan(), JsonOptions)
            ?? throw new InvalidDataException("The publication recovery journal is empty.");
        ValidateJournal(loadedJournal, project.ProjectId, operationId);
        return new(project, files, loadedJournal, snapshot.Version, checkpoint: null);
    }

    /// <summary>Restores unfinished work before any new cook or runtime mount.</summary>
    /// <param name="writer">The exclusive recovery owner.</param>
    /// <returns>Completion after prior roots and metadata have been restored.</returns>
    public async Task RecoverAsync(CookOutputWriteLease writer)
    {
        writer.VerifyOwner(this.project.ProjectRoot);
        if (this.Phase is CookPublicationPhase.Committed or CookPublicationPhase.RolledBack)
        {
            return;
        }

        await this.RestoreFilesAsync().ConfigureAwait(false);
        await this.WriteJournalAsync(CookPublicationPhase.RolledBack, CancellationToken.None).ConfigureAwait(false);
    }

    /// <summary>Removes only verified operation-owned output after commit or successful restoration.</summary>
    /// <param name="writer">The exclusive cleanup owner.</param>
    /// <returns>Completion after private roots have been removed.</returns>
    public async Task CleanupAsync(CookOutputWriteLease writer)
    {
        writer.VerifyOwner(this.project.ProjectRoot);
        if (this.Phase is not (CookPublicationPhase.Committed or CookPublicationPhase.RolledBack))
        {
            throw new InvalidOperationException("Unfinished publication retains its recovery files.");
        }

        var owned = new List<string>();
        foreach (var root in this.journal.Roots)
        {
            foreach (var area in new[] { "previous", "discarded", "output" })
            {
                var image = await this.ReadAreaAsync(area, root.Mount).ConfigureAwait(false);
                if (!image.Exists)
                {
                    continue;
                }

                var expected = string.Equals(area, "previous", StringComparison.Ordinal) ? root.Before : root.After;
                if (!image.Matches(expected))
                {
                    throw new InvalidDataException("Private publication files changed; cleanup retained them for review.");
                }

                owned.Add(this.RootPath(area, root.Mount));
            }
        }

        foreach (var path in owned)
        {
            Directory.Delete(path, recursive: true);
        }
    }

    /// <summary>Verifies the current committed generation before it is mounted.</summary>
    /// <param name="writer">The exclusive recovery owner.</param>
    /// <returns>Completion if current root and metadata bytes match their committed identities.</returns>
    public async Task VerifyCommittedAsync(CookOutputWriteLease writer)
    {
        writer.VerifyOwner(this.project.ProjectRoot);
        if (this.Phase != CookPublicationPhase.Committed)
        {
            throw new InvalidOperationException("The current publication has not committed.");
        }

        foreach (var root in this.journal.Roots)
        {
            if (!root.After.Matches(await this.ReadAreaAsync("published", root.Mount).ConfigureAwait(false)))
            {
                throw new InvalidDataException($"Committed cooked content changed in '{root.Mount}'.");
            }
        }

        foreach (var metadata in this.journal.Metadata)
        {
            if ((await this.files.ReadAsync(this.MetadataPath(metadata), CancellationToken.None).ConfigureAwait(false)).Version != Version(metadata.After))
            {
                throw new InvalidDataException("Committed publication metadata changed.");
            }
        }
    }

    private static void ValidateJournal(CookPublicationJournal journal, Guid projectId, Guid operationId)
    {
        if (journal.Version != 1 || journal.ProjectId != projectId || journal.OperationId != operationId
            || !Enum.IsDefined(journal.Phase) || journal.Roots.IsDefaultOrEmpty || journal.Metadata.IsDefaultOrEmpty
            || journal.Roots.Any(static root => root is null) || journal.Metadata.Any(static file => file is null || file.After is null)
            || !journal.Metadata.Any(static file => string.Equals(file.RelativePath, PublicationMetadata, StringComparison.Ordinal))
            || journal.Metadata.Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal).Count != journal.Metadata.Length)
        {
            throw new InvalidDataException("The publication journal does not identify a complete operation for this project.");
        }

        try
        {
            _ = CookStagingArea.ValidateMounts(journal.Roots.Select(static root => root.Mount));
        }
        catch (ArgumentException exception)
        {
            throw new InvalidDataException("The publication journal contains invalid mount names.", exception);
        }

        foreach (var root in journal.Roots)
        {
            ValidateImage(root.Before);
            ValidateImage(root.After);
            if (!root.After.Exists || !root.After.Files.ContainsKey("container.index.bin"))
            {
                throw new InvalidDataException("The journal's new root has no cooked index.");
            }
        }

        foreach (var metadata in journal.Metadata)
        {
            ValidateMetadataPath(metadata.RelativePath);
        }
    }

    private static void ValidateImage(CookRootImage image)
    {
        if (image?.Files is null || (!image.Exists && image.Files.Count != 0)
            || image.Files.Any(static item => string.IsNullOrEmpty(item.Key) || Path.IsPathRooted(item.Key)
                || item.Key.Split('/').Any(static part => part is "" or "." or "..") || item.Key.Contains('\\', StringComparison.Ordinal)
                || item.Value is null || item.Value.Size < 0 || item.Value.Sha256 is not { Length: 64 }
                || !item.Value.Sha256.All(Uri.IsHexDigit)))
        {
            throw new InvalidDataException("The publication journal contains an invalid root identity.");
        }
    }

    private async Task RestoreFilesAsync()
    {
        // Verify all recovery material before changing any root.
        foreach (var root in this.journal.Roots)
        {
            var previous = await this.ReadAreaAsync("previous", root.Mount).ConfigureAwait(false);
            var published = await this.ReadAreaAsync("published", root.Mount).ConfigureAwait(false);
            var staged = await this.ReadAreaAsync("output", root.Mount).ConfigureAwait(false);
            if ((previous.Exists && !previous.Matches(root.Before))
                || (root.Before.Exists && !previous.Exists && !published.Matches(root.Before))
                || (!root.Before.Exists && published.Exists && staged.Exists)
                || (published.Exists && !published.Matches(root.Before) && !published.Matches(root.After)))
            {
                throw new InvalidDataException($"Recovery material for '{root.Mount}' is missing or changed. Files have been retained.");
            }
        }

        foreach (var root in this.journal.Roots.Reverse())
        {
            var previous = await this.ReadAreaAsync("previous", root.Mount).ConfigureAwait(false);
            var published = await this.ReadAreaAsync("published", root.Mount).ConfigureAwait(false);
            if (published.Matches(root.Before))
            {
                continue;
            }

            if (published.Exists)
            {
                MoveOwnedRoot(this.RootPath("published", root.Mount), this.RootPath("discarded", root.Mount));
            }

            if (previous.Exists)
            {
                MoveOwnedRoot(this.RootPath("previous", root.Mount), this.RootPath("published", root.Mount));
            }
        }

        foreach (var metadata in this.journal.Metadata.Reverse())
        {
            var path = this.MetadataPath(metadata);
            var current = await this.files.ReadAsync(path, CancellationToken.None).ConfigureAwait(false);
            if (current.Version == Version(metadata.Before))
            {
                continue;
            }

            if (current.Version != Version(metadata.After))
            {
                throw new StorageWriteConflictException($"Publication metadata changed outside this transaction: '{path}'.");
            }

            if (metadata.Before is null)
            {
                File.Delete(path);
            }
            else
            {
                _ = await this.files.WriteAsync(path, metadata.Before, current.Version, CancellationToken.None).ConfigureAwait(false);
            }
        }
    }

    private Task<CookRootImage> ReadAreaAsync(string area, string mount)
        => CookRootImage.CaptureAsync(this.RootPath(area, mount), copyTo: null, CancellationToken.None);
}
