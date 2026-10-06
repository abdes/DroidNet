// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.SnapshotScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Snapshots;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SnapshotCaptureTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Names dirty participating documents and releases their save gates without capturing bytes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DirtyDependencyRequiresSaveBeforeCapture()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/material.omat.json", "saved material");
        var state = State(input) with { Revision = 2, IsDirty = true };
        var released = false;
        using var registration = workspace.Documents.Register(
            input.SourcePath,
            _ => Task.FromResult<CookDocumentReadLease?>(new(state, () => released = true)));

        var result = await workspace.CaptureAsync([input]).ConfigureAwait(false);

        _ = result.Snapshot.Should().BeNull();
        _ = result.NeedsSave.Should().ContainSingle().Which.Should().Be(state);
        _ = released.Should().BeTrue();
        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build"), "*", SearchOption.AllDirectories).Should().BeEmpty();
    }

    /// <summary>Does not block a material cook on a dirty scene that merely consumes that material.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DirtyConsumerOutsideInputClosureDoesNotBlockCapture()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/material.omat.json", "saved material");
        var consumer = workspace.WriteInput("Content/scene.oscene.json", "saved scene");
        var acquired = false;
        using var registration = workspace.Documents.Register(consumer.SourcePath, _ =>
        {
            acquired = true;
            return Task.FromResult<CookDocumentReadLease?>(new(State(consumer) with { IsDirty = true }, () => { }));
        });

        var result = await workspace.CaptureAsync([input]).ConfigureAwait(false);

        _ = result.Snapshot.Should().NotBeNull();
        _ = result.NeedsSave.Should().BeEmpty();
        _ = acquired.Should().BeFalse();
    }

    /// <summary>Retains captured saved bytes and revisions when their original source changes later.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task LaterSourceChangesDoNotMutateSnapshot()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/material.omat.json", "saved material");
        var state = State(input);
        using var registration = workspace.Documents.Register(
            input.SourcePath,
            _ => Task.FromResult<CookDocumentReadLease?>(new(state, () => { })));

        var unnormalized = input with
        {
            SourcePath = Path.Combine(workspace.Root, "Content", "..", "Content", "material.omat.json"),
        };
        var result = await workspace.CaptureAsync([unnormalized]).ConfigureAwait(false);
        _ = result.Snapshot.Should().NotBeNull();
        var snapshot = result.Snapshot!;
        await File.WriteAllTextAsync(input.SourcePath, "later saved material", CancellationToken.None).ConfigureAwait(false);

        _ = (await File.ReadAllTextAsync(Path.Combine(snapshot.InputRoot, input.RelativePath), CancellationToken.None).ConfigureAwait(false)).Should().Be("saved material");
        _ = snapshot.Documents.Should().ContainSingle().Which.SavedRevision.Should().Be(1);
        _ = snapshot.Inputs.Should().ContainSingle().Which.SourcePath.Should().Be(input.SourcePath);
    }

    /// <summary>Retries discovery when source bytes changed between dependency discovery and capture.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DiscoveryChangeRetriesTheWholeInputSet()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/source.bin", "before");
        var discoveries = 0;

        var result = await workspace.CaptureAsync(async token =>
        {
            discoveries++;
            if (discoveries == 1)
            {
                await File.WriteAllTextAsync(input.SourcePath, "after", token).ConfigureAwait(false);
                return [input];
            }

            return [input with { DiscoveryHash = Hash("after") }];
        }).ConfigureAwait(false);

        _ = result.Snapshot.Should().NotBeNull();
        var snapshot = result.Snapshot!;
        _ = discoveries.Should().Be(2);
        _ = (await File.ReadAllTextAsync(Path.Combine(snapshot.InputRoot, input.RelativePath), CancellationToken.None).ConfigureAwait(false)).Should().Be("after");
        _ = Directory.GetDirectories(Path.GetDirectoryName(snapshot.InputRoot)!).Should().ContainSingle();
    }

    /// <summary>Distinguishes external file changes from unsaved document edits.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ExternalChangeRequiresReloadInsteadOfSilentCapture()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/material.omat.json", "external material");
        var state = State(input) with { SavedContentHash = Hash("document's saved material") };
        using var registration = workspace.Documents.Register(
            input.SourcePath,
            _ => Task.FromResult<CookDocumentReadLease?>(new(state, () => { })));

        var result = await workspace.CaptureAsync([input]).ConfigureAwait(false);

        _ = result.Snapshot.Should().BeNull();
        _ = result.NeedsSave.Should().BeEmpty();
        _ = result.ExternalChanges.Should().ContainSingle().Which.Should().Be(state);
        _ = Directory.GetFiles(Path.Combine(workspace.Root, ".build"), "*", SearchOption.AllDirectories).Should().BeEmpty();
    }

    /// <summary>Releases coordinated document reads and partial output when a dependency is missing.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task FailedCaptureReleasesDocumentReadsAndPartialFiles()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/material.omat.json", "saved material");
        var released = false;
        using var registration = workspace.Documents.Register(
            input.SourcePath,
            _ => Task.FromResult<CookDocumentReadLease?>(new(State(input), () => released = true)));
        File.Delete(input.SourcePath);

        var capture = () => workspace.CaptureAsync([input]);
        _ = await capture.Should().ThrowAsync<FileNotFoundException>().ConfigureAwait(false);

        _ = released.Should().BeTrue();
        _ = Directory.GetFiles(Path.Combine(workspace.Root, ".build"), "*", SearchOption.AllDirectories).Should().BeEmpty();
    }

    /// <summary>Rejects paths that could escape the input directory or name ambiguous Windows outputs.</summary>
    /// <param name="relativePath">An invalid snapshot target path.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow("../outside.bin")]
    [DataRow("Content/file:stream")]
    [DataRow("Content/file.")]
    [DataRow("Content//file")]
    public async Task InvalidSnapshotPathsAreRejected(string relativePath)
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/source.bin", "saved source") with { RelativePath = relativePath };

        var capture = () => workspace.CaptureAsync([input]);
        _ = await capture.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);

        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build"), "*", SearchOption.AllDirectories).Should().BeEmpty();
    }

    /// <summary>Uses logical source content and compatible artifacts, independently of workspace and operation paths.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InputIdentityIsPortableAndIncludesTheBuildFingerprint()
    {
        using var first = new CaptureWorkspace();
        using var second = new CaptureWorkspace();
        var firstInput = first.WriteInput("Content/source.bin", "same bytes");
        var secondInput = second.WriteInput("Content/source.bin", "same bytes");
        var original = (await first.CaptureAsync([firstInput]).ConfigureAwait(false)).Snapshot!;
        var relocated = (await second.CaptureAsync([secondInput with { RelativePath = "Content\\source.bin" }]).ConfigureAwait(false)).Snapshot!;
        var changedBuild = (await second.CaptureAsync([secondInput], "different-compatible-build").ConfigureAwait(false)).Snapshot!;

        _ = relocated.InputIdentity.Should().Be(original.InputIdentity);
        _ = relocated.InputRoot.Should().NotBe(original.InputRoot);
        _ = changedBuild.InputIdentity.Should().NotBe(original.InputIdentity);
    }

    /// <summary>Rejects stale callbacks even if their project remains active.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ReleasedOperationCannotCaptureInputs()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/source.bin", "saved source");
        var operation = await workspace.Coordinator.RunAsync((current, _) => Task.FromResult(current), CancellationToken.None).ConfigureAwait(false);

        var capture = () => workspace.Capture.CaptureAsync(
            operation,
            _ => Task.FromResult<IReadOnlyList<CookSnapshotInput>>([input]),
            "compatible-build",
            CancellationToken.None);
        _ = await capture.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build"), "*", SearchOption.AllDirectories).Should().BeEmpty();
    }

    /// <summary>Rejects relative asset identity before moving any captured input into its final directory.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RelativeAssetIdentityCannotLeaveAnUnownedSnapshot()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/source.bin", "saved source") with { AssetUri = new Uri("source.bin", UriKind.Relative) };

        var capture = () => workspace.CaptureAsync([input]);
        _ = await capture.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);

        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build"), "*", SearchOption.AllDirectories).Should().BeEmpty();
    }

    private static CookDocumentState State(CookSnapshotInput input)
        => new(Guid.NewGuid(), input.SourcePath, "Material", 1, 1, IsDirty: false, input.DiscoveryHash);
}
