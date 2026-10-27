// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Restores source and head state while retaining immutable generations for readers.</summary>
internal sealed partial class CookPublicationTransaction
{
    internal static async Task<CookPublicationTransaction> LoadAsync(
        ProjectContext project,
        Guid operationId,
        IAtomicFileStore files,
        IProjectManagerService manager,
        CookOutputWriteLease gate,
        CancellationToken cancellationToken)
    {
        gate.VerifyOwner(project.ProjectRoot);
        var directory = Path.Combine(project.ProjectRoot.FullPath, ".build", "cook", operationId.ToString("N"));
        CookOutputLease.RejectReparsePoint(directory);
        var path = Path.Combine(directory, "publication.json");
        CookOutputLease.RejectReparsePoint(path);
        var snapshot = await files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        if (!snapshot.Version.Exists)
        {
            throw new InvalidDataException("The publication recovery journal is missing.");
        }

        var loaded = JsonSerializer.Deserialize<CookPublicationJournal>(snapshot.Content.AsSpan(), JsonOptions)
            ?? throw new InvalidDataException("The publication recovery journal is empty.");
        ValidateJournal(loaded, project.ProjectId, operationId);
        var transaction = new CookPublicationTransaction(project, files, manager, loaded, snapshot.Version, checkpoint: null);
        foreach (var file in loaded.SourceFiles)
        {
            if (MayHaveMutated(loaded.Phase))
            {
                _ = ProducedSourcePath(project, file);
                if (loaded.SourceReplacement is { } replacement && IsWithinSourceBundle(project, replacement.BundleRoot, file.RelativePath))
                {
                    throw new InvalidDataException("A source update overlaps a source-directory replacement.");
                }
            }
        }

        if (loaded.SourceReplacement is { } source)
        {
            try
            {
                _ = Import.ImportSourceRetention.ResolveBundleRoot(project, source.BundleRoot);
            }
            catch (Exception invalid) when (invalid is ArgumentException or InvalidDataException or InvalidOperationException)
            {
                throw new InvalidDataException("The recovery journal contains an invalid source bundle.", invalid);
            }

            ValidateImage(source.Before);
            ValidateImage(source.After);
            if (!source.Before.Exists || !source.After.Exists || source.After.Files.IsEmpty)
            {
                throw new InvalidDataException("A source replacement lacks its before/after identities.");
            }
        }

        if (loaded.ProjectChange is { } configuration)
        {
            _ = transaction.ParseProjectConfiguration(configuration.BeforeJson);
            _ = transaction.ParseProjectConfiguration(configuration.AfterJson);
        }

        return transaction;
    }

    // The caller already owns the selection gate. Never acquire it recursively
    // and never wait for an operation lock while holding it.
    internal static async Task RecoverInterruptedMutationsAsync(
        ProjectContext project,
        IAtomicFileStore files,
        IProjectManagerService manager,
        CookOutputWriteLease gate,
        Guid? currentOperation,
        CancellationToken cancellationToken)
    {
        gate.VerifyOwner(project.ProjectRoot);
        foreach (var id in await PendingMutationsAsync(project, files, currentOperation, cancellationToken).ConfigureAwait(false))
        {
            using var operation = CookOutputLease.AcquireOperation(project.ProjectRoot, id);
            var saved = await manager.LoadProjectInfoAsync(project.ProjectRoot.FullPath).ConfigureAwait(false)
                ?? throw new InvalidDataException("The saved project is unavailable for publication recovery.");
            if (saved.Id != project.ProjectId)
            {
                throw new InvalidDataException("Publication recovery encountered another project's configuration.");
            }

            var recoveryProject = ProjectContext.FromProjectInfo(saved, project.FileSystem);
            var pending = await LoadAsync(recoveryProject, id, files, manager, gate, cancellationToken).ConfigureAwait(false);
            await pending.RecoverAsync(gate).ConfigureAwait(false);
        }
    }

    internal static async Task RequireSettledSelectionAsync(
        ProjectContext project,
        IAtomicFileStore files,
        CookOutputWriteLease gate,
        CancellationToken cancellationToken)
    {
        gate.VerifyOwner(project.ProjectRoot);
        if ((await PendingMutationsAsync(project, files, currentOperation: null, cancellationToken).ConfigureAwait(false)).Count != 0)
        {
            throw new CookOutputBusyException("Cook or reopen the project to recover an interrupted publication.");
        }
    }

    internal async Task RecoverAsync(CookOutputWriteLease gate)
    {
        gate.VerifyOwner(this.project.ProjectRoot);
        if (this.Phase is CookPublicationPhase.Committed or CookPublicationPhase.RolledBack)
        {
            return;
        }

        if (MayHaveMutated(this.Phase))
        {
            await this.RestoreFilesAsync().ConfigureAwait(false);
        }

        await this.WriteJournalAsync(CookPublicationPhase.RolledBack, CancellationToken.None).ConfigureAwait(false);
    }

    private static async Task<List<Guid>> PendingMutationsAsync(
        ProjectContext project,
        IAtomicFileStore files,
        Guid? currentOperation,
        CancellationToken cancellationToken)
    {
        var pending = new List<Guid>();
        var operations = Path.Combine(project.ProjectRoot.FullPath, ".build", "cook");
        foreach (var directory in Directory.EnumerateDirectories(operations))
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!Guid.TryParseExact(Path.GetFileName(directory), "N", out var id) || id == currentOperation)
            {
                continue;
            }

            CookOutputLease.RejectReparsePoint(directory);
            var path = Path.Combine(directory, "publication.json");
            CookOutputLease.RejectReparsePoint(path);
            var bytes = await files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
            if (!bytes.Version.Exists)
            {
                continue;
            }

            var recorded = JsonSerializer.Deserialize<CookPublicationJournal>(bytes.Content.AsSpan(), JsonOptions)
                ?? throw new InvalidDataException("A publication recovery journal is empty.");
            ValidateJournal(recorded, project.ProjectId, id);
            if (!MayHaveMutated(recorded.Phase))
            {
                continue;
            }

            pending.Add(id);
        }

        return pending;
    }

    private static bool MayHaveMutated(CookPublicationPhase phase)
        => phase is CookPublicationPhase.Applying or CookPublicationPhase.SourcesApplied
            or CookPublicationPhase.HeadSelected or CookPublicationPhase.RuntimeReady;

    private static void ValidateJournal(CookPublicationJournal journal, Guid projectId, Guid operationId)
    {
        if (journal.Version != CookPublicationJournal.CurrentVersion || journal.ProjectId != projectId || journal.OperationId != operationId
            || !Enum.IsDefined(journal.Phase) || journal.CandidateGenerations.IsDefault
            || journal.CandidateGenerations.Any(static id => id == Guid.Empty)
            || journal.CandidateGenerations.Distinct().Take(journal.CandidateGenerations.Length + 1).Count() != journal.CandidateGenerations.Length
            || journal.SourceFiles.IsDefault || journal.SourceFiles.Any(static file => file is null)
            || journal.SourceFiles.Select(static file => file.RelativePath).Distinct(FileSystemPath.NameComparer).Take(journal.SourceFiles.Length + 1).Count() != journal.SourceFiles.Length
            || (journal.Phase is not (CookPublicationPhase.Building or CookPublicationPhase.RolledBack) && journal.CandidateHead is null))
        {
            throw new InvalidDataException("The publication journal does not identify a complete operation for this project.");
        }

        ValidateHeadSnapshot(journal.PreviousHead);
        if (journal.CandidateHead is { } candidate)
        {
            ValidateHeadSnapshot(candidate);
            var head = JsonSerializer.Deserialize<CookPublicationHead>(candidate.Content.AsSpan(), JsonOptions);
            if (!candidate.Version.Exists || head?.PublicationId != operationId)
            {
                throw new InvalidDataException("The candidate head belongs to another operation.");
            }
        }
    }

    private static void ValidateHeadSnapshot(FileSnapshot snapshot)
    {
        if (snapshot?.Version is null || snapshot.Content.IsDefault
            || snapshot.Version != (snapshot.Version.Exists ? Version([.. snapshot.Content]) : FileVersion.Missing)
            || (!snapshot.Version.Exists && !snapshot.Content.IsEmpty))
        {
            throw new InvalidDataException("A journaled head has an invalid byte identity.");
        }

        if (snapshot.Version.Exists)
        {
            var head = JsonSerializer.Deserialize<CookPublicationHead>(snapshot.Content.AsSpan(), JsonOptions);
            if (head is null || head.Version != CookPublicationHead.CurrentVersion || head.PublicationId == Guid.Empty
                || !CookPublicationDocument.IsDigest(head.DocumentSha256))
            {
                throw new InvalidDataException("A journaled publication head is invalid.");
            }
        }
    }

    private static void ValidateImage(CookRootImage image)
    {
        if (image?.Files is null || (!image.Exists && image.Files.Count != 0)
            || image.Files.Any(static item => string.IsNullOrEmpty(item.Key) || Path.IsPathRooted(item.Key)
                || item.Key.Split('/').Any(static part => part is "" or "." or "..") || item.Key.Contains('\\', StringComparison.Ordinal)
                || item.Value is null || item.Value.Size < 0 || !CookPublicationDocument.IsDigest(item.Value.Sha256)))
        {
            throw new InvalidDataException("The source journal contains an invalid directory identity.");
        }
    }

    private async Task RestoreFilesAsync()
    {
        var headPath = CookPublicationPaths.Head(this.project.ProjectRoot.FullPath);
        var head = await this.files.ReadAsync(headPath, CancellationToken.None).ConfigureAwait(false);
        if (head.Version != this.PreviousHead.Version && head.Version != this.journal.CandidateHead?.Version)
        {
            throw new StorageWriteConflictException("The publication head changed outside this transaction.");
        }

        foreach (var update in this.journal.SourceFiles)
        {
            var current = await this.files.ReadAsync(ProducedSourcePath(this.project, update), CancellationToken.None).ConfigureAwait(false);
            if (current.Version != Version(update.Before) && current.Version != Version(update.After))
            {
                throw new StorageWriteConflictException("Source settings changed outside this transaction.");
            }
        }

        var restoreProject = await this.RequiresProjectRestoreAsync().ConfigureAwait(false);
        await this.VerifyRecoveryMaterialAsync().ConfigureAwait(false);
        await this.RestoreSourceFilesAsync().ConfigureAwait(false);
        foreach (var directory in this.Directories().Reverse())
        {
            var published = await CookRootImage.CaptureAsync(directory.Published, copyTo: null, CancellationToken.None).ConfigureAwait(false);
            if (published.Matches(directory.Before))
            {
                continue;
            }

            if (published.Exists)
            {
                MoveOwnedRoot(directory.Published, directory.Discarded);
            }

            MoveOwnedRoot(directory.Previous, directory.Published);
        }

        if (restoreProject && this.journal.ProjectChange is { } configuration)
        {
            var restored = this.ParseProjectConfiguration(configuration.BeforeJson);
            var superseded = this.ParseProjectConfiguration(configuration.AfterJson);
            await this.projectManager.SaveProjectInfoAsync(restored, superseded, CancellationToken.None).ConfigureAwait(false);
        }

        if (head.Version != this.PreviousHead.Version)
        {
            if (this.PreviousHead.Version.Exists)
            {
                _ = await this.files.WriteAsync(headPath, this.PreviousHead.Content.ToArray(), head.Version, CancellationToken.None).ConfigureAwait(false);
            }
            else
            {
                File.Delete(headPath);
            }
        }
    }

    // Reports whether the saved project configuration is still the post-change one and must be rolled back; throws
    // when it is neither the before nor the after configuration.
    private async Task<bool> RequiresProjectRestoreAsync()
    {
        if (this.journal.ProjectChange is not { } change)
        {
            return false;
        }

        var actual = await this.projectManager.LoadProjectInfoAsync(this.project.ProjectRoot.FullPath).ConfigureAwait(false)
            ?? throw new InvalidDataException("The project's configuration cannot be read for recovery.");
        var json = ProjectInfo.ToJson(actual);
        var before = ProjectInfo.ToJson(this.ParseProjectConfiguration(change.BeforeJson));
        var after = ProjectInfo.ToJson(this.ParseProjectConfiguration(change.AfterJson));
        if (!string.Equals(json, before, StringComparison.Ordinal) && !string.Equals(json, after, StringComparison.Ordinal))
        {
            throw new StorageWriteConflictException("The project configuration changed outside this transaction.");
        }

        return !string.Equals(json, before, StringComparison.Ordinal);
    }

    // Confirms every retained and published source directory is in a state recovery can restore from.
    private async Task VerifyRecoveryMaterialAsync()
    {
        foreach (var directory in this.Directories())
        {
            var previous = await CookRootImage.CaptureAsync(directory.Previous, copyTo: null, CancellationToken.None).ConfigureAwait(false);
            var published = await CookRootImage.CaptureAsync(directory.Published, copyTo: null, CancellationToken.None).ConfigureAwait(false);
            if ((previous.Exists && !previous.Matches(directory.Before))
                || (!previous.Exists && !published.Matches(directory.Before))
                || (published.Exists && !published.Matches(directory.Before) && !published.Matches(directory.After)))
            {
                throw new InvalidDataException($"Source recovery material for '{directory.Name}' is missing or changed.");
            }
        }
    }
}
