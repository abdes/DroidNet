// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.PublicationScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class PublicationRecoveryTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Recovery restores prior roots even when a rename completed before the next journal update.</summary>
    /// <param name="hadPrevious">Whether prior output existed.</param>
    /// <param name="boundary">The interrupted durable boundary.</param>
    /// <returns>The asynchronous recovery regression.</returns>
    [TestMethod]
    [DataRow(true, "Applying")]
    [DataRow(true, "SourcesApplied")]
    [DataRow(true, "HeadSelected")]
    [DataRow(true, "RuntimeReady")]
    [DataRow(false, "Applying")]
    [DataRow(false, "SourcesApplied")]
    [DataRow(false, "HeadSelected")]
    [DataRow(false, "RuntimeReady")]
    public async Task PersistedInterruptionRestoresTheCompletePriorState(bool hadPrevious, string boundary)
    {
        using var project = new PublicationProject(hadPrevious);
        var replica = Directory.CreateTempSubdirectory("oxygen-publication-recovery-");
        try
        {
            await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
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
            Func<Task> publish = () => transaction.PublishAsync(preview: null, project.Baseline, static () => { }, this.TestContext.CancellationToken);
            _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
            _ = captured.Should().BeTrue();
            var reopened = project.Context with { ProjectRoot = replica.FullName };
            using var writer = await CookOutputLease.AcquireWriteAsync(replica.FullName, this.TestContext.CancellationToken).ConfigureAwait(false);
            var recovery = await CookPublicationTransaction.LoadAsync(reopened, project.Operation.OperationId, project.Files, project.Manager, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
            await recovery.RecoverAsync(writer).ConfigureAwait(false);
            _ = recovery.Phase.Should().Be(CookPublicationPhase.RolledBack);
            AssertRecoveredRoot(replica.FullName, "Content", hadPrevious);
            AssertRecoveredRoot(replica.FullName, "Second", hadPrevious);

        }
        finally
        {
            replica.Delete(recursive: true);
        }
    }

    /// <summary>Recovery cannot replace a head written outside the interrupted transaction.</summary>
    [TestMethod]
    public async Task ChangedHeadBlocksRecoveryAndRetainsJournal()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var head = CookPublicationPaths.Head(project.Root);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, async name =>
        {
            if (name == "HeadSelected")
            {
                await File.WriteAllTextAsync(head, "external selection", this.TestContext.CancellationToken).ConfigureAwait(false);
                throw new IOException("Injected external head change");
            }
        }).ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(preview: null, project.Baseline, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<AggregateException>().ConfigureAwait(false);
        _ = transaction.Phase.Should().Be(CookPublicationPhase.HeadSelected);
        using var gate = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var recovery = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, project.Manager, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> recover = () => recovery.RecoverAsync(gate);
        _ = await recover.Should().ThrowAsync<DroidNet.Storage.StorageWriteConflictException>().ConfigureAwait(false);
        _ = File.ReadAllText(head).Should().Be("external selection");
        _ = File.Exists(Path.Combine(project.Root, ".build", "cook", project.Operation.OperationId.ToString("N"), "publication.json")).Should().BeTrue();
    }

    /// <summary>A journal names contained generations by valid identities rather than arbitrary physical paths.</summary>
    /// <returns>The asynchronous journal-validation regression.</returns>
    [TestMethod]
    public async Task InvalidGenerationIdentityIsRejectedBeforeRecovery()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(project.Root, ".build", "cook", project.Operation.OperationId.ToString("N"), "publication.json");
        var journal = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        journal["candidateGenerations"]![0] = Guid.Empty.ToString("D");
        await File.WriteAllTextAsync(path, journal.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> read = async () => _ = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, project.Manager, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await read.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        project.AssertOld();
    }

    /// <summary>Committed content is checked by bytes before mounting, including unchanged size and timestamp.</summary>
    /// <returns>The asynchronous committed-output regression.</returns>
    [TestMethod]
    public async Task CommittedOutputMustStillMatchItsProof()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var accepted = await transaction.PublishAsync(preview: null, project.Baseline, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        await staging.DisposeAsync().ConfigureAwait(false);
        var path = Path.Combine(accepted.FindProjectRoot("Content")!, "container.index.bin");
        var timestamp = File.GetLastWriteTimeUtc(path);
        var bytes = await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        bytes[^1] ^= 1;
        await File.WriteAllBytesAsync(path, bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        File.SetLastWriteTimeUtc(path, timestamp);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var loaded = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, project.Manager, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> verify = async () =>
        {
            using var catalog = await accepted.CreateCatalogAsync(accepted.Roots.Single(static root => root.Name == "Content"), this.TestContext.CancellationToken).ConfigureAwait(false);
        };
        _ = await verify.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
    }
}
