// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Reclaims unused output after its operation and reader lifetimes end.</summary>
public sealed partial class CookPublicationService
{
    /// <summary>Reclaims unused publications, generations and drained scratch without waiting for readers.</summary>
    /// <param name="project">The owner of generated output.</param>
    /// <param name="cancellationToken">Cancels between claims and deletions.</param>
    /// <returns>Cleanup failures; committed content remains selected.</returns>
    public Task<IReadOnlyList<string>> MaintainAsync(ProjectContext project, CancellationToken cancellationToken)
        => this.MaintainCoreAsync(project, completedOperation: null, cancellationToken);

    internal Task<IReadOnlyList<string>> MaintainAfterWorkAsync(ContentCookOperation operation)
    {
        coordinator.VerifyWriter(operation);
        return this.MaintainCoreAsync(operation.Project, operation.OperationId, CancellationToken.None);
    }

    private async Task<IReadOnlyList<string>> MaintainCoreAsync(ProjectContext project, Guid? completedOperation, CancellationToken token)
    {
        using var batch = new MaintenanceBatch();
        using (var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, token).ConfigureAwait(false))
        {
            var path = Path.Combine(project.ProjectRoot, ".build", "cook", "maintenance.lock");
            CookOutputLease.RejectReparsePoint(path);
            batch.Maintenance = WindowsCookFile.TryOpenExclusive(path, FileMode.OpenOrCreate, out _);
            if (batch.Maintenance is null)
            {
                return batch.Failures;
            }

            await CookPublicationTransaction.RequireSettledSelectionAsync(project, files, gate, token).ConfigureAwait(false);
            using var selected = await CookPublicationReadLease.OpenUnderGateAsync(project, files, gate, token).ConfigureAwait(false);
            if (selected.PublicationId is { } id)
            {
                batch.RetainedDocuments.Add(id);
            }

            batch.RetainedGenerations.UnionWith(selected.Roots.Where(static root => root.Owner == CookPublicationRootOwner.Project).Select(static root => root.SourceKey));
            await this.ClaimOperationsAsync(project, gate, completedOperation, batch, token).ConfigureAwait(false);
            await ClaimDocumentsAsync(project, batch, token).ConfigureAwait(false);
            ClaimGenerations(project, batch);
        }

        await Task.Run(() =>
        {
            DeleteClaimed(project, batch, token);
            batch.ReleaseClaims();
            RemoveEmptyOperationDirectories(batch);
        }, token).ConfigureAwait(false);
        return batch.Failures;
    }

    private async Task ClaimOperationsAsync(ProjectContext project, CookOutputWriteLease gate, Guid? completedOperation, MaintenanceBatch batch, CancellationToken token)
    {
        foreach (var (id, directory) in OwnedDirectories(Path.Combine(project.ProjectRoot, ".build", "cook")))
        {
            token.ThrowIfCancellationRequested();
            var handle = CookOutputLease.TryAcquireOperation(project.ProjectRoot, id);
            var owned = handle is null ? null : new OperationClaim(directory, handle);
            if (owned is not null)
            {
                batch.Operations.Add(owned);
            }

            var journal = Path.Combine(directory, "publication.json");
            CookOutputLease.RejectReparsePoint(journal);
            if (File.Exists(journal))
            {
                var transaction = await CookPublicationTransaction.LoadAsync(project, id, files, projectManager, gate, token).ConfigureAwait(false);
                if (owned is null)
                {
                    if (id != completedOperation || transaction.Phase != CookPublicationPhase.Committed)
                    {
                        batch.RetainedDocuments.Add(id);
                        RetainHead(transaction.PreviousHead, batch.RetainedDocuments);
                        batch.RetainedGenerations.UnionWith(transaction.CandidateGenerations);
                    }
                }
                else
                {
                    owned.Transaction = transaction;
                    await transaction.RecoverAsync(gate).ConfigureAwait(false);
                    if (!transaction.HasPreparedHead)
                    {
                        batch.UnpreparedGenerations.UnionWith(transaction.CandidateGenerations);
                    }
                }
            }

            // Up-to-date and preflight-only operations may have scratch without a journal.
            owned?.Retry = CookOutputLease.TryAcquireRetryInput(project.ProjectRoot, id);
        }
    }

    private static async Task ClaimDocumentsAsync(ProjectContext project, MaintenanceBatch batch, CancellationToken token)
    {
        var parent = Path.Combine(project.ProjectRoot, ".cooked", "publications");
        CookOutputLease.RejectReparsePoint(parent);
        if (!Directory.Exists(parent))
        {
            return;
        }

        foreach (var path in Directory.EnumerateFiles(parent, "*.json"))
        {
            if (!Guid.TryParseExact(Path.GetFileNameWithoutExtension(path), "N", out var id))
            {
                continue;
            }

            CookOutputLease.RejectReparsePoint(path);
            var claim = batch.RetainedDocuments.Contains(id) ? null : WindowsCookFile.TryClaimPublication(path);
            if (claim is not null)
            {
                batch.Documents.Add(new(path, claim));
                continue;
            }

            var bytes = await File.ReadAllBytesAsync(path, token).ConfigureAwait(false);
            var document = JsonSerializer.Deserialize<CookPublicationDocument>(bytes, CookPublicationDocument.JsonOptions)
                ?? throw new InvalidDataException("A retained publication is empty.");
            document.Validate(project);
            if (document.OperationId != id)
            {
                throw new InvalidDataException("A retained publication has the wrong identity.");
            }

            batch.RetainedGenerations.UnionWith(document.Roots.Where(static root => root.Owner == CookPublicationRootOwner.Project).Select(static root => root.SourceKey));
        }
    }

    private static void ClaimGenerations(ProjectContext project, MaintenanceBatch batch)
    {
        foreach (var (id, path) in OwnedDirectories(Path.Combine(project.ProjectRoot, ".cooked", "generations")))
        {
            if (batch.RetainedGenerations.Contains(id))
            {
                continue;
            }

            var generation = batch.UnpreparedGenerations.Contains(id) ? CookedGeneration.TryClaimAbandoned(path) : CookedGeneration.TryClaim(path);
            if (generation is not null)
            {
                batch.Generations.Add(generation);
            }
        }
    }

    private static void DeleteClaimed(ProjectContext project, MaintenanceBatch batch, CancellationToken token)
    {
        foreach (var generation in batch.Generations)
        {
            token.ThrowIfCancellationRequested();
            batch.Attempt(generation.Delete);
        }

        foreach (var document in batch.Documents)
        {
            token.ThrowIfCancellationRequested();
            batch.Attempt(() => File.Delete(document.Path));
        }

        foreach (var operation in batch.Operations.Where(static item => item.Retry is not null))
        {
            token.ThrowIfCancellationRequested();
            var retainJournal = operation.Transaction is { HasPreparedHead: false } transaction
                && transaction.CandidateGenerations.Select(id => CookPublicationPaths.Generation(project.ProjectRoot, id))
                    .Any(path => Directory.Exists(path) && !File.Exists(Path.Combine(path, CookedGeneration.MarkerFileName)));
            batch.Attempt(() => DeleteOperationPayloads(operation.Directory, retainJournal));
        }
    }

    private static void RemoveEmptyOperationDirectories(MaintenanceBatch batch)
    {
        foreach (var operation in batch.Operations.Where(static item => item.Retry is not null))
        {
            batch.Attempt(() =>
            {
                if (Directory.Exists(operation.Directory) && !Directory.EnumerateFileSystemEntries(operation.Directory).Any())
                {
                    // Do not recurse after closing the ownership markers.
                    Directory.Delete(operation.Directory);
                }
            });
        }
    }

    private static void DeleteOperationPayloads(string directory, bool retainJournal)
    {
        foreach (var path in Directory.EnumerateFileSystemEntries(directory))
        {
            if (Path.GetFileName(path) is not ("operation.lock" or "retry-input.lock" or "publication.json"))
            {
                DeleteOwnedEntry(path);
            }
        }

        if (!retainJournal)
        {
            File.Delete(Path.Combine(directory, "publication.json"));
        }

        static void DeleteOwnedEntry(string path)
        {
            CookOutputLease.RejectReparsePoint(path);
            if (!Directory.Exists(path))
            {
                File.Delete(path);
                return;
            }

            foreach (var child in Directory.EnumerateFileSystemEntries(path))
            {
                DeleteOwnedEntry(child);
            }

            Directory.Delete(path);
        }
    }

    private static IEnumerable<(Guid id, string path)> OwnedDirectories(string parent)
    {
        CookOutputLease.RejectReparsePoint(parent);
        if (!Directory.Exists(parent))
        {
            yield break;
        }

        foreach (var path in Directory.EnumerateDirectories(parent))
        {
            if (Guid.TryParseExact(Path.GetFileName(path), "N", out var id) && id != Guid.Empty)
            {
                CookOutputLease.RejectReparsePoint(path);
                yield return (id, path);
            }
        }
    }

    private static void RetainHead(FileSnapshot snapshot, HashSet<Guid> documents)
    {
        if (snapshot.Version.Exists)
        {
            var head = JsonSerializer.Deserialize<CookPublicationHead>(snapshot.Content.AsSpan(), CookPublicationDocument.JsonOptions)
                ?? throw new InvalidDataException("A recovery head is empty.");
            documents.Add(head.PublicationId);
        }
    }

    private sealed class MaintenanceBatch : IDisposable
    {
        internal FileStream? Maintenance { get; set; }

        internal List<OperationClaim> Operations { get; } = [];

        internal List<DocumentClaim> Documents { get; } = [];

        internal List<CookedGeneration> Generations { get; } = [];

        internal HashSet<Guid> RetainedDocuments { get; } = [];

        internal HashSet<Guid> RetainedGenerations { get; } = [];

        internal HashSet<Guid> UnpreparedGenerations { get; } = [];

        internal List<string> Failures { get; } = [];

        internal void Attempt(Action deletion)
        {
            try
            {
                deletion();
            }
            catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
            {
                this.Failures.Add(failure.Message);
            }
        }

        internal void ReleaseClaims()
        {
            foreach (var generation in this.Generations) { generation.Dispose(); }
            foreach (var document in this.Documents) { document.Dispose(); }
            foreach (var operation in this.Operations) { operation.Dispose(); }
        }

        public void Dispose()
        {
            this.ReleaseClaims();
            this.Maintenance?.Dispose();
        }
    }

    private sealed class OperationClaim(string directory, FileStream ownership) : IDisposable
    {
        internal string Directory { get; } = directory;

        internal CookPublicationTransaction? Transaction { get; set; }

        internal FileStream? Retry { get; set; }

        public void Dispose()
        {
            this.Retry?.Dispose();
            ownership.Dispose();
        }
    }

    private sealed class DocumentClaim(string path, FileStream handle) : IDisposable
    {
        internal string Path { get; } = path;

        public void Dispose() => handle.Dispose();
    }
}
