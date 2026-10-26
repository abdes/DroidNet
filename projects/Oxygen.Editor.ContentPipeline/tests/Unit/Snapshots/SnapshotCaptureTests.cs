// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.SnapshotScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Snapshots;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SnapshotCaptureTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Retains captured saved bytes and revisions when their original source changes later.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task LaterSourceChangesDoNotMutateSnapshot()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/material.omat.json", "saved material");

        var unnormalized = input with
        {
            SourcePath = FileSystemPath.From(Path.Combine(workspace.Root, "Content", "..", "Content", "material.omat.json")),
        };
        var snapshot = await workspace.CaptureAsync([unnormalized]).ConfigureAwait(false);
        await File.WriteAllTextAsync(input.SourcePath.FullPath, "later saved material", CancellationToken.None).ConfigureAwait(false);

        _ = (await File.ReadAllTextAsync(Path.Combine(snapshot.InputRoot, input.RelativePath), CancellationToken.None).ConfigureAwait(false)).Should().Be("saved material");
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

        var snapshot = await workspace.CaptureAsync(async token =>
        {
            discoveries++;
            if (discoveries == 1)
            {
                await File.WriteAllTextAsync(input.SourcePath.FullPath, "after", token).ConfigureAwait(false);
                return [input];
            }

            return [input with { DiscoveryHash = Hash("after") }];
        }).ConfigureAwait(false);

        _ = discoveries.Should().Be(2);
        _ = (await File.ReadAllTextAsync(Path.Combine(snapshot.InputRoot, input.RelativePath), CancellationToken.None).ConfigureAwait(false)).Should().Be("after");
        _ = Directory.GetDirectories(Path.GetDirectoryName(snapshot.InputRoot)!).Should().ContainSingle();
    }

    /// <summary>Leaves no partial output when a dependency is missing.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task FailedCaptureLeavesNoPartialFiles()
    {
        using var workspace = new CaptureWorkspace();
        var input = workspace.WriteInput("Content/material.omat.json", "saved material");
        File.Delete(input.SourcePath.FullPath);

        var capture = () => workspace.CaptureAsync([input]);
        _ = await capture.Should().ThrowAsync<FileNotFoundException>().ConfigureAwait(false);

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

    /// <summary>Uses logical source content, independently of workspace and operation paths.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InputIdentityIsPortable()
    {
        using var first = new CaptureWorkspace();
        using var second = new CaptureWorkspace();
        var firstInput = first.WriteInput("Content/source.bin", "same bytes");
        var secondInput = second.WriteInput("Content/source.bin", "same bytes");
        var original = await first.CaptureAsync([firstInput]).ConfigureAwait(false);
        var relocated = await second.CaptureAsync([secondInput with { RelativePath = "Content\\source.bin" }]).ConfigureAwait(false);

        _ = relocated.InputIdentity.Should().Be(original.InputIdentity);
        _ = relocated.InputRoot.Should().NotBe(original.InputRoot);
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
            CancellationToken.None);
        _ = await capture.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build"), "*", SearchOption.AllDirectories).Should().BeEmpty();
    }
}
