// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Recovers the actual persisted filesystem images at publication boundaries.</summary>
public sealed partial class CookPublicationTransactionTests
{
    /// <summary>Recovery restores prior roots even when a rename completed before the next journal update.</summary>
    /// <param name="hadPrevious">Whether prior output existed.</param>
    /// <param name="boundary">The interrupted durable boundary.</param>
    /// <returns>The asynchronous recovery regression.</returns>
    [TestMethod]
    [DataRow(true, "Retained:Content")]
    [DataRow(true, "Retained:Second")]
    [DataRow(true, "OldRetained")]
    [DataRow(true, "Installed:Content")]
    [DataRow(true, "Installed:Second")]
    [DataRow(true, "RootsInstalled")]
    [DataRow(true, "RuntimeReady")]
    [DataRow(true, "Metadata:.build/cook/provenance.json")]
    [DataRow(true, "Metadata:.cooked/publication.json")]
    [DataRow(false, "Installed:Content")]
    [DataRow(false, "Metadata:.cooked/publication.json")]
    public async Task PersistedInterruptionRestoresTheCompletePriorState(bool hadPrevious, string boundary)
    {
        using var project = new PublicationProject(hadPrevious);
        var replica = Directory.CreateTempSubdirectory("oxygen-publication-recovery-");
        try
        {
            using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
            var captured = false;
            var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, async name =>
            {
                if (string.Equals(name, boundary, StringComparison.Ordinal))
                {
                    await CopyPersistedStateAsync(project.Root, replica.FullName, this.TestContext.CancellationToken).ConfigureAwait(false);
                    captured = true;
                    throw new IOException("Stop after capturing interrupted state");
                }
            }).ConfigureAwait(false);
            Func<Task> publish = () => transaction.PublishAsync(preview: null, static () => { }, this.TestContext.CancellationToken);
            _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
            _ = captured.Should().BeTrue();
            var reopened = project.Context with { ProjectRoot = replica.FullName };
            using var writer = await CookOutputLease.AcquireWriteAsync(replica.FullName, this.TestContext.CancellationToken).ConfigureAwait(false);
            var recovery = await CookPublicationTransaction.LoadAsync(reopened, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
            await recovery.RecoverAsync(writer).ConfigureAwait(false);
            _ = recovery.Phase.Should().Be(CookPublicationPhase.RolledBack);
            AssertRecoveredRoot(replica.FullName, "Content", hadPrevious);
            AssertRecoveredRoot(replica.FullName, "Second", hadPrevious);
            AssertRecoveredMetadata(replica.FullName, CookPublicationTransaction.PublicationMetadata, "old-receipt", hadPrevious);
            AssertRecoveredMetadata(replica.FullName, CookPublicationTransaction.ProvenanceMetadata, "old-provenance", hadPrevious);
        }
        finally
        {
            replica.Delete(recursive: true);
        }
    }

    /// <summary>Missing rollback material preserves both the installed output and the remaining backup.</summary>
    /// <returns>The asynchronous recovery-material regression.</returns>
    [TestMethod]
    public async Task ChangedBackupBlocksRecoveryAndRetainsEvidence()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var operationRoot = Path.Combine(project.Root, ".build", "cook", project.Operation.OperationId.ToString("N"));
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, async name =>
        {
            if (string.Equals(name, "RootsInstalled", StringComparison.Ordinal))
            {
                await File.WriteAllTextAsync(Path.Combine(operationRoot, "previous", "Content", "container.index.bin"), "changed backup", this.TestContext.CancellationToken).ConfigureAwait(false);
                throw new IOException("Injected failure after backup changed");
            }
        }).ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(preview: null, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<AggregateException>().ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.RootsInstalled);
        _ = (await File.ReadAllTextAsync(Path.Combine(operationRoot, "previous", "Second", "container.index.bin"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("old:Second");
        _ = (await File.ReadAllTextAsync(Path.Combine(project.Root, ".cooked", "Second", "container.index.bin"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("new:Second");
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var recovery = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> restore = async () => await recovery.RecoverAsync(writer).ConfigureAwait(false);
        _ = await restore.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        _ = File.Exists(Path.Combine(operationRoot, "publication.json")).Should().BeTrue();
    }

    /// <summary>A journal cannot redirect recovery outside its project-owned root names.</summary>
    /// <returns>The asynchronous journal-validation regression.</returns>
    [TestMethod]
    public async Task JournalTraversalIsRejectedBeforeFilesAreMoved()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(project.Root, ".build", "cook", project.Operation.OperationId.ToString("N"), "publication.json");
        var journal = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        journal["roots"]![0]!["mount"] = "../outside";
        await File.WriteAllTextAsync(path, journal.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> read = async () => _ = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await read.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        project.AssertOld();
    }

    /// <summary>Committed content is checked by bytes before mounting, including unchanged size and timestamp.</summary>
    /// <returns>The asynchronous committed-output regression.</returns>
    [TestMethod]
    public async Task CommittedOutputMustStillMatchItsProof()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        await transaction.PublishAsync(preview: null, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(project.Root, ".cooked", "Content", "container.index.bin");
        var timestamp = File.GetLastWriteTimeUtc(path);
        await File.WriteAllTextAsync(path, "bad:Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        File.SetLastWriteTimeUtc(path, timestamp);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var loaded = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> verify = async () => await loaded.VerifyCommittedAsync(writer).ConfigureAwait(false);
        _ = await verify.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }

    private static async Task CopyPersistedStateAsync(string source, string target, CancellationToken cancellationToken)
    {
        foreach (var path in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
        {
            _ = Directory.CreateDirectory(Path.Combine(target, Path.GetRelativePath(source, path)));
        }

        foreach (var path in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            if (Path.GetExtension(path) is ".lock" or ".lease")
            {
                continue;
            }

            var bytes = await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false);
            await File.WriteAllBytesAsync(Path.Combine(target, Path.GetRelativePath(source, path)), bytes, cancellationToken).ConfigureAwait(false);
        }
    }

    private static void AssertRecoveredRoot(string projectRoot, string mount, bool hadPrevious)
    {
        var root = Path.Combine(projectRoot, ".cooked", mount);
        if (hadPrevious)
        {
            _ = File.ReadAllText(Path.Combine(root, "container.index.bin")).Should().Be("old:" + mount);
            _ = File.ReadAllText(Path.Combine(root, "keep.bin")).Should().Be("keep:" + mount);
        }
        else
        {
            _ = Directory.Exists(root).Should().BeFalse();
        }
    }

    private static void AssertRecoveredMetadata(string projectRoot, string relative, string expected, bool hadPrevious)
    {
        var path = Path.Combine(projectRoot, relative);
        if (hadPrevious)
        {
            _ = File.ReadAllText(path).Should().Be(expected);
        }
        else
        {
            _ = File.Exists(path).Should().BeFalse();
        }
    }
}
