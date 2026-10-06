// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Commits authored-source changes and one publication head; cooked generations never move.</summary>
internal sealed partial class CookPublicationTransaction
{
    private static readonly JsonSerializerOptions JsonOptions = CookPublicationDocument.JsonOptions;
    private readonly ProjectContext project;
    private readonly IAtomicFileStore files;
    private readonly IProjectManagerService projectManager;
    private readonly string operationDirectory;
    private readonly Func<string, Task> checkpoint;
    private CookPublicationJournal journal;
    private FileVersion journalVersion;

    private CookPublicationTransaction(ProjectContext project, IAtomicFileStore files, IProjectManagerService projectManager,
        CookPublicationJournal journal, FileVersion journalVersion, Func<string, Task>? checkpoint)
    {
        this.project = project;
        this.files = files;
        this.projectManager = projectManager;
        this.journal = journal;
        this.journalVersion = journalVersion;
        this.operationDirectory = Path.Combine(project.ProjectRoot, ".build", "cook", journal.OperationId.ToString("N"));
        this.checkpoint = checkpoint ?? (_ => Task.CompletedTask);
    }

    internal CookPublicationPhase Phase => this.journal.Phase;

    internal FileSnapshot PreviousHead => this.journal.PreviousHead;

    internal FileSnapshot CandidateHead => this.journal.CandidateHead
        ?? throw new InvalidOperationException("The publication has not been prepared.");

    internal ImmutableArray<Guid> CandidateGenerations => this.journal.CandidateGenerations;

    internal bool HasPreparedHead => this.journal.CandidateHead is not null;

    internal Task AbandonBuildAsync() => this.Phase == CookPublicationPhase.Building
        ? this.WriteJournalAsync(CookPublicationPhase.RolledBack, CancellationToken.None) : Task.CompletedTask;

    internal static async Task<CookPublicationTransaction> ReserveAsync(ContentCookOperation operation,
        CookPublicationReadLease baseline, ImmutableArray<Guid> generations, IAtomicFileStore files,
        IProjectManagerService projectManager, CancellationToken cancellationToken, Func<string, Task>? checkpoint = null)
    {
        if (generations.Any(static id => id == Guid.Empty) || generations.Distinct().Count() != generations.Length)
        {
            throw new InvalidDataException("Candidate generation identities must be distinct and nonempty.");
        }

        var journal = new CookPublicationJournal(CookPublicationJournal.CurrentVersion, operation.Project.ProjectId,
            operation.OperationId, CookPublicationPhase.Building, generations, baseline.Head, CandidateHead: null);
        var transaction = new CookPublicationTransaction(operation.Project, files, projectManager, journal, FileVersion.Missing, checkpoint);
        await transaction.WriteJournalAsync(CookPublicationPhase.Building, cancellationToken).ConfigureAwait(false);
        return transaction;
    }

    internal async Task PrepareAsync(ContentCookOperation operation, CookPublicationDocument document,
        CookSourceReplacement? sourceReplacement, ImmutableArray<CookProducedSourceFile> producedSourceFiles,
        CookPublicationJournal.ProjectConfiguration? projectChange, CancellationToken cancellationToken)
    {
        if (this.Phase != CookPublicationPhase.Building || operation.OperationId != this.journal.OperationId
            || document.OperationId != operation.OperationId || operation.Project.ProjectId != this.project.ProjectId)
        {
            throw new InvalidOperationException("Only the originating operation can prepare its reserved publication.");
        }

        document.Validate(this.project);
        if (!this.journal.CandidateGenerations.All(id => document.Roots.Any(root => root.Owner == CookPublicationRootOwner.Project && root.SourceKey == id)))
        {
            throw new InvalidDataException("The publication omitted a reserved cooked generation.");
        }

        producedSourceFiles = producedSourceFiles.IsDefault ? [] : producedSourceFiles;
        if (producedSourceFiles.Select(static file => file.RelativePath).Distinct(StringComparer.OrdinalIgnoreCase).Count() != producedSourceFiles.Length)
        {
            throw new InvalidDataException("A publication cannot update the same source settings twice.");
        }

        var sources = ImmutableArray.CreateBuilder<CookProducedSourceFile>();
        foreach (var update in producedSourceFiles)
        {
            var path = ProducedSourcePath(this.project, update);
            if (sourceReplacement is not null && IsWithinSourceBundle(this.project, sourceReplacement.BundleName, update.RelativePath))
            {
                continue;
            }

            if ((await this.files.ReadAsync(path, cancellationToken).ConfigureAwait(false)).Version != Version(update.Before))
            {
                throw new StorageWriteConflictException("Source settings changed while cooking. Retry from the current source.");
            }

            sources.Add(update);
        }

        if (projectChange is not null)
        {
            _ = this.ParseProjectConfiguration(projectChange.BeforeJson);
            _ = this.ParseProjectConfiguration(projectChange.AfterJson);
        }

        var configured = projectChange is null ? this.project
            : ProjectContext.FromProjectInfo(this.ParseProjectConfiguration(projectChange.AfterJson), []);
        if (!string.Equals(document.MountConfigurationIdentity, CookPublicationDocument.ConfigurationIdentity(configured), StringComparison.Ordinal))
        {
            throw new InvalidDataException("The candidate publication does not describe its intended mount configuration.");
        }

        var sourceBundle = sourceReplacement is null ? null
            : await CaptureSourceReplacementAsync(operation, sourceReplacement, producedSourceFiles, cancellationToken).ConfigureAwait(false);
        var pathToDocument = CookPublicationPaths.Document(this.project.ProjectRoot, document.OperationId);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(pathToDocument)!);
        var documentVersion = await this.files.WriteAsync(pathToDocument, JsonSerializer.SerializeToUtf8Bytes(document, JsonOptions), FileVersion.Missing, cancellationToken).ConfigureAwait(false);
        var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, documentVersion.Sha256);
        var bytes = JsonSerializer.SerializeToUtf8Bytes(head, JsonOptions);
        this.journal = this.journal with
        {
            CandidateHead = new([.. bytes], Version(bytes)),
            SourceReplacement = sourceBundle,
            SourceFiles = sources.ToImmutable(),
            ProjectChange = projectChange,
        };
        await this.WriteJournalAsync(CookPublicationPhase.Prepared, cancellationToken).ConfigureAwait(false);
    }

    internal async Task<CookPublicationReadLease> PublishAsync(ICookPublicationPreview? preview, CookPublicationReadLease previous,
        Action verifyOwner, CancellationToken cancellationToken)
    {
        if (this.Phase != CookPublicationPhase.Prepared || previous.Head.Version != this.PreviousHead.Version)
        {
            throw new InvalidOperationException("Publication requires its prepared candidate and captured baseline.");
        }

        verifyOwner();
        using var priorLifetime = previous.Retain();
        CookPublicationReadLease candidate;
        using (var preparationGate = await CookOutputLease.AcquireWriteAsync(this.project.ProjectRoot, cancellationToken).ConfigureAwait(false))
        {
            candidate = await CookPublicationReadLease.OpenDocumentUnderGateAsync(this.project, this.files, preparationGate,
                this.CandidateHead, cancellationToken).ConfigureAwait(false);
        }

        var transferred = false;
        CookedContentMountSet? admission = null;
        try
        {
            candidate.RequireAvailableGenerations();
            // Keep metadata admission and member opens outside the selection gate.
            // These readers also protect mutable external libraries through commit.
            admission = await new CookedContentMountService().PrepareAsync(this.project, candidate, cancellationToken).ConfigureAwait(false);
            using var gate = await CookOutputLease.AcquireWriteAsync(this.project.ProjectRoot, cancellationToken).ConfigureAwait(false);
            await RecoverInterruptedMutationsAsync(this.project, this.files, this.projectManager, gate, this.journal.OperationId, cancellationToken).ConfigureAwait(false);
            await this.VerifyBaselinesAsync(cancellationToken).ConfigureAwait(false);
            verifyOwner();
            cancellationToken.ThrowIfCancellationRequested();
            var mutated = false;
            var previewTouched = false;
            try
            {
                if (preview is not null)
                {
                    previewTouched = true;
                    await preview.PrepareReplacementAsync().ConfigureAwait(false);
                }

                await this.WriteJournalAsync(CookPublicationPhase.Applying, CancellationToken.None).ConfigureAwait(false);
                await this.checkpoint("Applying").ConfigureAwait(false);
                mutated = true;
                await this.InstallSourceChangesAsync(verifyOwner).ConfigureAwait(false);
                await this.WriteJournalAsync(CookPublicationPhase.SourcesApplied, CancellationToken.None).ConfigureAwait(false);
                await this.checkpoint("SourcesApplied").ConfigureAwait(false);
                _ = await this.files.WriteAsync(CookPublicationPaths.Head(this.project.ProjectRoot), this.CandidateHead.Content.ToArray(),
                    this.PreviousHead.Version, CancellationToken.None).ConfigureAwait(false);
                await this.WriteJournalAsync(CookPublicationPhase.HeadSelected, CancellationToken.None).ConfigureAwait(false);
                await this.checkpoint("HeadSelected").ConfigureAwait(false);
                if (preview is not null)
                {
                    var handoff = admission;
                    admission = null;
                    await preview.MountAsync(handoff).ConfigureAwait(false);
                }

                await this.WriteJournalAsync(CookPublicationPhase.RuntimeReady, CancellationToken.None).ConfigureAwait(false);
                await this.checkpoint("RuntimeReady").ConfigureAwait(false);
                verifyOwner();
                if (this.journal.ProjectChange is { } configuration)
                {
                    await this.projectManager.SaveProjectInfoAsync(this.ParseProjectConfiguration(configuration.AfterJson),
                        this.ParseProjectConfiguration(configuration.BeforeJson), CancellationToken.None).ConfigureAwait(false);
                    await this.checkpoint("ProjectConfigurationSaved").ConfigureAwait(false);
                }

                if (preview is not null)
                {
                    await preview.ResumeAsync().ConfigureAwait(false);
                }

                verifyOwner();
                await this.WriteJournalAsync(CookPublicationPhase.Committed, CancellationToken.None).ConfigureAwait(false);
                transferred = true;
                return candidate;
            }
            catch (Exception failure)
            {
                try
                {
                    if (previewTouched && preview is not null)
                    {
                        await preview.PrepareReplacementAsync().ConfigureAwait(false);
                    }

                    if (mutated)
                    {
                        await this.RestoreFilesAsync().ConfigureAwait(false);
                    }

                    if (previewTouched && preview is not null && priorLifetime.UnavailableRoots.IsEmpty)
                    {
                        var restored = await new CookedContentMountService().PrepareAsync(this.project, priorLifetime, CancellationToken.None).ConfigureAwait(false);
                        await preview.MountAsync(restored).ConfigureAwait(false);
                        await preview.ResumeAsync().ConfigureAwait(false);
                    }

                    await this.WriteJournalAsync(CookPublicationPhase.RolledBack, CancellationToken.None).ConfigureAwait(false);
                }
                catch (Exception rollback)
                {
                    throw new AggregateException($"Publication recovery is required at '{this.operationDirectory}'.", failure, rollback);
                }

                throw;
            }
        }
        finally
        {
            admission?.Dispose();
            if (!transferred)
            {
                candidate.Dispose();
            }
        }
    }

    private static FileVersion Version(byte[]? bytes) => bytes is null ? FileVersion.Missing
        : new(Exists: true, Convert.ToHexString(SHA256.HashData(bytes)));

    private static void MoveOwnedRoot(string source, string destination)
    {
        CookOutputLease.RejectReparsePoint(source);
        CookOutputLease.RejectReparsePoint(Path.GetDirectoryName(destination)!);
        CookOutputLease.RejectReparsePoint(destination);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        Directory.Move(source, destination);
    }

    private string RootPath(string area, string name)
    {
        var parent = Path.Combine(this.operationDirectory, area);
        CookOutputLease.RejectReparsePoint(parent);
        return Path.Combine(parent, name);
    }

    private async Task WriteJournalAsync(CookPublicationPhase phase, CancellationToken cancellationToken)
    {
        var updated = this.journal with { Phase = phase };
        var path = Path.Combine(this.operationDirectory, "publication.json");
        CookOutputLease.RejectReparsePoint(path);
        this.journalVersion = await this.files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(updated, JsonOptions), this.journalVersion, cancellationToken).ConfigureAwait(false);
        this.journal = updated;
    }

    private async Task VerifyBaselinesAsync(CancellationToken cancellationToken)
    {
        if ((await this.files.ReadAsync(CookPublicationPaths.Head(this.project.ProjectRoot), cancellationToken).ConfigureAwait(false)).Version != this.PreviousHead.Version)
        {
            throw new StorageWriteConflictException("The cooked publication changed while cooking. Retry from the current publication.");
        }

        var currentProject = await this.projectManager.LoadProjectInfoAsync(this.project.ProjectRoot).ConfigureAwait(false)
            ?? throw new InvalidDataException("The project's saved configuration is unavailable.");
        var configurationMatches = this.journal.ProjectChange is { } change
            ? string.Equals(ProjectInfo.ToJson(currentProject), ProjectInfo.ToJson(this.ParseProjectConfiguration(change.BeforeJson)), StringComparison.Ordinal)
            : string.Equals(CookPublicationDocument.ConfigurationIdentity(ProjectContext.FromProjectInfo(currentProject, [])),
                CookPublicationDocument.ConfigurationIdentity(this.project), StringComparison.Ordinal);
        if (!configurationMatches)
        {
            throw new StorageWriteConflictException("The project's content mounts changed while publication was being prepared.");
        }

        foreach (var update in this.journal.SourceFiles)
        {
            if ((await this.files.ReadAsync(ProducedSourcePath(this.project, update), cancellationToken).ConfigureAwait(false)).Version != Version(update.Before))
            {
                throw new StorageWriteConflictException("Source settings changed after publication preparation.");
            }
        }

        foreach (var directory in this.Directories())
        {
            var current = await CookRootImage.CaptureAsync(directory.Published, copyTo: null, cancellationToken).ConfigureAwait(false);
            var staged = await CookRootImage.CaptureAsync(directory.Staged, copyTo: null, cancellationToken).ConfigureAwait(false);
            if (!directory.Before.Matches(current) || !directory.After.Matches(staged))
            {
                throw new StorageWriteConflictException($"Source changed after preparing '{directory.Name}'.");
            }
        }
    }

    private async Task InstallSourceChangesAsync(Action verifyOwner)
    {
        foreach (var directory in this.Directories())
        {
            verifyOwner();
            MoveOwnedRoot(directory.Published, directory.Previous);
            await this.checkpoint("Retained:" + directory.Name).ConfigureAwait(false);
            MoveOwnedRoot(directory.Staged, directory.Published);
            await this.checkpoint("Installed:" + directory.Name).ConfigureAwait(false);
        }

        foreach (var update in this.journal.SourceFiles)
        {
            verifyOwner();
            _ = await this.files.WriteAsync(ProducedSourcePath(this.project, update), update.After, Version(update.Before), CancellationToken.None).ConfigureAwait(false);
            await this.checkpoint("SourceFile:" + update.RelativePath).ConfigureAwait(false);
        }
    }

    private IProjectInfo ParseProjectConfiguration(string json)
    {
        var configuration = ProjectInfo.FromJson(json);
        if (configuration.Id != this.project.ProjectId)
        {
            throw new InvalidDataException("A publication cannot change another project's configuration.");
        }

        configuration.Location = this.project.ProjectRoot;
        return configuration;
    }
}
