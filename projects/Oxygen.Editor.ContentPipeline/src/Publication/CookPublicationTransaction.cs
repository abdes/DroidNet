// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using DroidNet.Storage;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Installs a validated root set with recoverable directory renames and metadata writes.</summary>
internal sealed partial class CookPublicationTransaction
{
    /// <summary>The published generation receipt.</summary>
    internal const string PublicationMetadata = ".cooked/publication.json";

    /// <summary>The incremental product cache restored with its publication.</summary>
    internal const string ProvenanceMetadata = ".build/cook/provenance.json";
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
        WriteIndented = true,
    };

    private readonly ProjectContext project;
    private readonly IAtomicFileStore files;
    private readonly string operationDirectory;
    private readonly Func<string, Task> checkpoint;
    private CookPublicationJournal journal;
    private FileVersion journalVersion;

    private CookPublicationTransaction(ProjectContext project, IAtomicFileStore files, CookPublicationJournal journal, FileVersion journalVersion, Func<string, Task>? checkpoint)
    {
        this.project = project;
        this.files = files;
        this.journal = journal;
        this.journalVersion = journalVersion;
        this.operationDirectory = Path.Combine(project.ProjectRoot, ".build", "cook", journal.OperationId.ToString("N"));
        this.checkpoint = checkpoint ?? (_ => Task.CompletedTask);
    }

    /// <summary>Gets the last recorded publication phase.</summary>
    public CookPublicationPhase Phase => this.journal.Phase;

    /// <summary>Gets a deferred private-file cleanup error after a successful commit.</summary>
    public Exception? CleanupFailure { get; private set; }

    /// <summary>Records all baselines before preview or published files are changed.</summary>
    /// <param name="operation">The live project operation.</param>
    /// <param name="staging">The validated staging roots.</param>
    /// <param name="metadata">New receipt and provenance bytes.</param>
    /// <param name="files">The atomic metadata store.</param>
    /// <param name="cancellationToken">Cancels before publication.</param>
    /// <param name="checkpoint">Optional controlled boundary observer.</param>
    /// <returns>The durable prepared transaction.</returns>
    public static async Task<CookPublicationTransaction> PrepareAsync(
        ContentCookOperation operation,
        CookStagingArea staging,
        IReadOnlyDictionary<string, byte[]> metadata,
        IAtomicFileStore files,
        CancellationToken cancellationToken,
        Func<string, Task>? checkpoint = null)
    {
        ArgumentNullException.ThrowIfNull(operation);
        ArgumentNullException.ThrowIfNull(staging);
        ArgumentNullException.ThrowIfNull(metadata);
        if (staging.Roots.IsDefaultOrEmpty || !metadata.ContainsKey(PublicationMetadata))
        {
            throw new InvalidDataException("Publication requires validated roots and a publication receipt.");
        }

        _ = CookStagingArea.ValidateMounts(staging.Roots.Select(static root => root.Mount));
        using var reader = CookOutputLease.AcquireRead(operation.Project.ProjectRoot);
        var roots = ImmutableArray.CreateBuilder<CookPublicationJournal.Root>();
        foreach (var root in staging.Roots)
        {
            var expected = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N"), "output", root.Mount);
            if (!string.Equals(Path.GetFullPath(root.StagingPath), Path.GetFullPath(expected), StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("Staging belongs to another cook operation.");
            }

            var after = await CookRootImage.CaptureAsync(root.StagingPath, copyTo: null, cancellationToken).ConfigureAwait(false);
            if (!after.Exists || !after.Files.ContainsKey("container.index.bin"))
            {
                throw new InvalidDataException($"Validated staging for '{root.Mount}' has no cooked index.");
            }

            roots.Add(new(root.Mount, root.Before, after));
        }

        var metadataFiles = ImmutableArray.CreateBuilder<CookPublicationJournal.MetadataFile>();
        foreach (var (relative, bytes) in metadata.OrderBy(static entry => entry.Key, StringComparer.Ordinal))
        {
            ValidateMetadataPath(relative);
            var before = await files.ReadAsync(Path.Combine(operation.Project.ProjectRoot, relative), cancellationToken).ConfigureAwait(false);
            metadataFiles.Add(new(relative, before.Version.Exists ? before.Content.ToArray() : null, bytes.ToArray()));
        }

        var preparedJournal = new CookPublicationJournal(1, operation.Project.ProjectId, operation.OperationId, CookPublicationPhase.Prepared, roots.ToImmutable(), metadataFiles.ToImmutable());
        var transaction = new CookPublicationTransaction(operation.Project, files, preparedJournal, FileVersion.Missing, checkpoint);
        await transaction.WriteJournalAsync(CookPublicationPhase.Prepared, cancellationToken).ConfigureAwait(false);
        staging.RetainForPublication();
        return transaction;
    }

    /// <summary>Publishes all roots and metadata, restoring prior state if any transition fails.</summary>
    /// <param name="preview">The current preview owner, or null for disk-only publication.</param>
    /// <param name="verifyOwner">Rejects a closed project or lost writer.</param>
    /// <param name="cancellationToken">Cancels before replacement; in-progress replacement reaches commit or rollback.</param>
    /// <returns>Completion after commit or an exception after restoration.</returns>
    public async Task PublishAsync(ICookPublicationPreview? preview, Action verifyOwner, CancellationToken cancellationToken)
    {
        if (this.Phase != CookPublicationPhase.Prepared)
        {
            throw new InvalidOperationException("Only a prepared publication can be installed.");
        }

        verifyOwner();
        cancellationToken.ThrowIfCancellationRequested();
        CookOutputWriteLease? writer = null;
        var mutationStarted = false;
        try
        {
            if (preview is not null)
            {
                await preview.PrepareReplacementAsync().ConfigureAwait(false);
            }

            writer = await CookOutputLease.AcquireWriteAsync(this.project.ProjectRoot, cancellationToken).ConfigureAwait(false);
            await this.VerifyBaselinesAsync(cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            verifyOwner();
            mutationStarted = true;
            await this.InstallRootsAsync(verifyOwner).ConfigureAwait(false);
            await this.CommitInstalledRootsAsync(preview, writer, verifyOwner).ConfigureAwait(false);
            await this.TryCleanupCommittedAsync(writer).ConfigureAwait(false);
        }
        catch (Exception failure)
        {
            try
            {
                if (preview is not null)
                {
                    await preview.PrepareReplacementAsync().ConfigureAwait(false);
                }

                if (mutationStarted)
                {
                    await this.RestoreFilesAsync().ConfigureAwait(false);
                }

                if (preview is not null)
                {
                    using var read = writer is null ? CookOutputLease.AcquireRead(this.project.ProjectRoot) : null;
                    await preview.MountAsync(this.GetPublishedRoots(), writer).ConfigureAwait(false);
                    await preview.ResumeAsync().ConfigureAwait(false);
                }

                await this.WriteJournalAsync(CookPublicationPhase.RolledBack, CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception rollback)
            {
                throw new AggregateException($"Cook publication and restoration failed. Recovery data is retained at '{this.operationDirectory}'.", failure, rollback);
            }

            throw;
        }
        finally
        {
            writer?.Dispose();
        }
    }

    private static FileVersion Version(byte[]? bytes)
        => bytes is null ? FileVersion.Missing : new(Exists: true, Convert.ToHexString(SHA256.HashData(bytes)));

    private static void ValidateMetadataPath(string relative)
    {
        if (relative is not (PublicationMetadata or ProvenanceMetadata))
        {
            throw new InvalidDataException("Publication may update only its receipt and product provenance cache.");
        }
    }

    private static void MoveOwnedRoot(string source, string destination)
    {
        CookOutputLease.RejectReparsePoint(source);
        CookOutputLease.RejectReparsePoint(Path.GetDirectoryName(destination)!);
        CookOutputLease.RejectReparsePoint(destination);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        Directory.Move(source, destination);
    }

    private async Task CommitInstalledRootsAsync(ICookPublicationPreview? preview, CookOutputWriteLease writer, Action verifyOwner)
    {
        if (preview is not null)
        {
            await preview.MountAsync(this.GetPublishedRoots(), writer).ConfigureAwait(false);
        }

        await this.WriteJournalAsync(CookPublicationPhase.RuntimeReady, CancellationToken.None).ConfigureAwait(false);
        await this.checkpoint("RuntimeReady").ConfigureAwait(false);
        verifyOwner();
        foreach (var metadata in this.journal.Metadata)
        {
            _ = await this.files.WriteAsync(this.MetadataPath(metadata), metadata.After, Version(metadata.Before), CancellationToken.None).ConfigureAwait(false);
            await this.checkpoint("Metadata:" + metadata.RelativePath).ConfigureAwait(false);
        }

        if (preview is not null)
        {
            await preview.ResumeAsync().ConfigureAwait(false);
        }

        verifyOwner();
        await this.WriteJournalAsync(CookPublicationPhase.Committed, CancellationToken.None).ConfigureAwait(false);
    }

    private async Task TryCleanupCommittedAsync(CookOutputWriteLease writer)
    {
        try
        {
            await this.CleanupAsync(writer).ConfigureAwait(false);
        }
        catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            this.CleanupFailure = cleanup;
        }
    }

    private string MetadataPath(CookPublicationJournal.MetadataFile metadata) => Path.Combine(this.project.ProjectRoot, metadata.RelativePath);

    private string RootPath(string area, string mount)
    {
        var parent = string.Equals(area, "published", StringComparison.Ordinal)
            ? Path.Combine(this.project.ProjectRoot, ".cooked") : Path.Combine(this.operationDirectory, area);
        CookOutputLease.RejectReparsePoint(parent);
        return Path.Combine(parent, mount);
    }

    private async Task WriteJournalAsync(CookPublicationPhase phase, CancellationToken cancellationToken)
    {
        var updated = this.journal with { Phase = phase };
        var path = Path.Combine(this.operationDirectory, "publication.json");
        this.journalVersion = await this.files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(updated, JsonOptions), this.journalVersion, cancellationToken).ConfigureAwait(false);
        this.journal = updated;
    }

    private async Task VerifyBaselinesAsync(CancellationToken cancellationToken)
    {
        foreach (var root in this.journal.Roots)
        {
            var current = await CookRootImage.CaptureAsync(this.RootPath("published", root.Mount), copyTo: null, cancellationToken).ConfigureAwait(false);
            var staged = await CookRootImage.CaptureAsync(this.RootPath("output", root.Mount), copyTo: null, cancellationToken).ConfigureAwait(false);
            if (!root.Before.Matches(current) || !root.After.Matches(staged))
            {
                throw new IOException($"Cooked content changed after staging '{root.Mount}'. Retry the cook.");
            }
        }

        foreach (var metadata in this.journal.Metadata)
        {
            if ((await this.files.ReadAsync(this.MetadataPath(metadata), cancellationToken).ConfigureAwait(false)).Version != Version(metadata.Before))
            {
                throw new StorageWriteConflictException("Publication metadata changed after staging. Retry the cook.");
            }
        }
    }

    private async Task InstallRootsAsync(Action verifyOwner)
    {
        foreach (var root in this.journal.Roots)
        {
            verifyOwner();
            if (root.Before.Exists)
            {
                MoveOwnedRoot(this.RootPath("published", root.Mount), this.RootPath("previous", root.Mount));
                await this.checkpoint("Retained:" + root.Mount).ConfigureAwait(false);
            }
        }

        await this.WriteJournalAsync(CookPublicationPhase.OldRetained, CancellationToken.None).ConfigureAwait(false);
        await this.checkpoint("OldRetained").ConfigureAwait(false);
        foreach (var root in this.journal.Roots)
        {
            verifyOwner();
            MoveOwnedRoot(this.RootPath("output", root.Mount), this.RootPath("published", root.Mount));
            await this.checkpoint("Installed:" + root.Mount).ConfigureAwait(false);
        }

        await this.WriteJournalAsync(CookPublicationPhase.RootsInstalled, CancellationToken.None).ConfigureAwait(false);
        await this.checkpoint("RootsInstalled").ConfigureAwait(false);
    }

    private string[] GetPublishedRoots()
    {
        var root = Path.Combine(this.project.ProjectRoot, ".cooked");
        CookOutputLease.RejectReparsePoint(root);
        return Directory.Exists(root) ? Directory.EnumerateDirectories(root)
            .Where(path => File.Exists(Path.Combine(path, "container.index.bin")))
            .Order(StringComparer.Ordinal).ToArray() : [];
    }
}
