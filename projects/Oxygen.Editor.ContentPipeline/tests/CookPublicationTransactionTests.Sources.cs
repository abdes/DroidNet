// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies that retained source replacement shares the cooked-output transaction.</summary>
public sealed partial class CookPublicationTransactionTests
{
    private const string SourceBundle = "Content/SourceMedia/DCC/Model";

    /// <summary>Source stays old during staging, commits with output, and remains editable afterwards.</summary>
    /// <returns>The asynchronous source publication regression.</returns>
    [TestMethod]
    public async Task SourceBundleCommitsWithCookedOutput()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var source = await this.StageSourceAsync(project).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, sourceReplacement: source).ConfigureAwait(false);
        AssertSource(project.Root, "old");
        await transaction.PublishAsync(preview: null, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertSource(project.Root, "new");
        project.AssertNew();
        await File.WriteAllTextAsync(Path.Combine(project.Root, SourceBundle, "model.glb"), "later edit", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var loaded = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        await loaded.VerifyCommittedAsync(writer).ConfigureAwait(false);
        await loaded.CleanupAsync(writer).ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(Path.Combine(project.Root, SourceBundle, "model.glb"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("later edit");
    }

    /// <summary>Failures and persisted interruptions restore source, settings, outputs and metadata together.</summary>
    /// <param name="boundary">The source/output transition interrupted.</param>
    /// <param name="persisted">Whether recovery loads a captured interrupted filesystem.</param>
    /// <returns>The asynchronous source rollback regression.</returns>
    [TestMethod]
    [DataRow("Retained:Source:Model", false)]
    [DataRow("Installed:Content", false)]
    [DataRow("Installed:Source:Model", false)]
    [DataRow("RuntimeReady", false)]
    [DataRow("Metadata:.cooked/publication.json", false)]
    [DataRow("Retained:Source:Model", true)]
    [DataRow("Installed:Content", true)]
    [DataRow("Installed:Source:Model", true)]
    [DataRow("RuntimeReady", true)]
    [DataRow("Metadata:.cooked/publication.json", true)]
    public async Task SourceBundleRestoresWithCookedOutput(string boundary, bool persisted)
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var source = await this.StageSourceAsync(project).ConfigureAwait(false);
        var replica = Directory.CreateTempSubdirectory("oxygen-source-recovery-");
        try
        {
            var transaction = await project.PrepareAsync(
                staging,
                this.TestContext.CancellationToken,
                async name =>
            {
                if (string.Equals(name, boundary, StringComparison.Ordinal))
                {
                    if (persisted)
                    {
                        await CopyPersistedStateAsync(project.Root, replica.FullName, this.TestContext.CancellationToken).ConfigureAwait(false);
                    }

                    throw new IOException("Injected source publication interruption");
                }
            },
                source).ConfigureAwait(false);
            Func<Task> publish = () => transaction.PublishAsync(preview: null, static () => { }, this.TestContext.CancellationToken);
            _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
            AssertSource(project.Root, "old");
            project.AssertOld();
            if (persisted)
            {
                using var writer = await CookOutputLease.AcquireWriteAsync(replica.FullName, this.TestContext.CancellationToken).ConfigureAwait(false);
                var recovery = await CookPublicationTransaction.LoadAsync(project.Context with { ProjectRoot = replica.FullName }, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
                await recovery.RecoverAsync(writer).ConfigureAwait(false);
                await recovery.CleanupAsync(writer).ConfigureAwait(false);
                AssertSource(replica.FullName, "old");
                AssertRecoveredRoot(replica.FullName, "Content", hadPrevious: true);
                AssertRecoveredRoot(replica.FullName, "Second", hadPrevious: true);
                AssertRecoveredMetadata(replica.FullName, CookPublicationTransaction.PublicationMetadata, "old-receipt", hadPrevious: true);
            }
        }
        finally
        {
            replica.Delete(recursive: true);
        }
    }

    /// <summary>A changed reviewed source is preserved before preparation or final installation.</summary>
    /// <param name="beforePreparation">Whether the edit precedes journal creation.</param>
    /// <returns>The asynchronous source conflict regression.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task SourceReplacementRejectsChangedBaseline(bool beforePreparation)
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var source = await this.StageSourceAsync(project).ConfigureAwait(false);
        var transaction = beforePreparation ? null : await project.PrepareAsync(staging, this.TestContext.CancellationToken, sourceReplacement: source).ConfigureAwait(false);
        var primary = Path.Combine(project.Root, SourceBundle, "model.glb");
        await File.WriteAllTextAsync(primary, "external edit", this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> apply = async () =>
        {
            transaction ??= await project.PrepareAsync(staging, this.TestContext.CancellationToken, sourceReplacement: source).ConfigureAwait(false);
            await transaction.PublishAsync(preview: null, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        };
        _ = await apply.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(primary, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("external edit");
        project.AssertOld();
    }

    /// <summary>Source replacement honors cancellation before mutation and completes a started transaction.</summary>
    /// <param name="afterMutation">Whether cancellation occurs after retaining the old bundle.</param>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task SourceReplacementCancellationHasOneOutcome(bool afterMutation)
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var source = await this.StageSourceAsync(project).ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        var transaction = await project.PrepareAsync(
                staging,
                this.TestContext.CancellationToken,
                async name =>
        {
            if (string.Equals(name, "Retained:Source:Model", StringComparison.Ordinal))
            {
                await cancellation.CancelAsync().ConfigureAwait(false);
            }
        },
                source).ConfigureAwait(false);
        if (afterMutation)
        {
            await transaction.PublishAsync(preview: null, static () => { }, cancellation.Token).ConfigureAwait(false);
            AssertSource(project.Root, "new");
            project.AssertNew();
        }
        else
        {
            await cancellation.CancelAsync().ConfigureAwait(false);
            Func<Task> publish = () => transaction.PublishAsync(preview: null, static () => { }, cancellation.Token);
            _ = await publish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
            AssertSource(project.Root, "old");
            project.AssertOld();
        }
    }

    /// <summary>A source journal cannot redirect recovery to a sibling or arbitrary authored directory.</summary>
    /// <returns>The asynchronous source-path guard regression.</returns>
    [TestMethod]
    public async Task SourceReplacementJournalRejectsTraversal()
    {
        using var project = new PublicationProject(hadPrevious: true);
        using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var source = await this.StageSourceAsync(project).ConfigureAwait(false);
        _ = await project.PrepareAsync(staging, this.TestContext.CancellationToken, sourceReplacement: source).ConfigureAwait(false);
        var path = Path.Combine(project.Root, ".build/cook", project.Operation.OperationId.ToString("N"), "publication.json");
        var journal = System.Text.Json.Nodes.JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        journal["sourceReplacement"]!["bundleName"] = "../Other";
        await File.WriteAllTextAsync(path, journal.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> load = async () => _ = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await load.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        AssertSource(project.Root, "old");
        project.AssertOld();
    }

    private static void AssertSource(string projectRoot, string expected)
    {
        foreach (var file in new[] { "model.glb", "model.glb.import.json", "buffers/data.bin" })
        {
            _ = File.ReadAllText(Path.Combine(projectRoot, SourceBundle, file)).Should().Be(expected + ":" + file);
        }
    }

    private async Task<CookSourceReplacement> StageSourceAsync(PublicationProject project)
    {
        var published = Path.Combine(project.Root, SourceBundle);
        var staged = Path.Combine(project.Root, ".build/cook", project.Operation.OperationId.ToString("N"), "inputs", SourceBundle);
        foreach (var (root, prefix) in new[] { (published, "old"), (staged, "new") })
        {
            foreach (var file in new[] { "model.glb", "model.glb.import.json", "buffers/data.bin" })
            {
                var path = Path.Combine(root, file);
                _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                await File.WriteAllTextAsync(path, prefix + ":" + file, this.TestContext.CancellationToken).ConfigureAwait(false);
            }
        }

        return new("Model", await CookRootImage.CaptureAsync(published, copyTo: null, this.TestContext.CancellationToken).ConfigureAwait(false));
    }
}
