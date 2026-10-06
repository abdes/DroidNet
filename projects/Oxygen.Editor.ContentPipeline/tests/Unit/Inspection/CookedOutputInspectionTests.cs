// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Testing;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class CookedOutputInspectionTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Project reports include every authoring mount and represent never-cooked roots without native work.</summary>
    /// <returns>The asynchronous multi-root scope regression.</returns>
    [TestMethod]
    public async Task ProjectInspectionIncludesAllOwnedRootsWithoutCookingMissingOutput()
    {
        using var workspace = new CookWorkspace([new("Cooked", ".cooked"), new("Content", "Content"), new("Extra", "MoreContent")]);
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator([]), api.Object);
        var report = await pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken, validate: true).ConfigureAwait(false);
        _ = report.Roots.Select(static root => root.Name).Should().Equal("Content", "Extra");
        _ = report.Roots.Should().OnlyContain(root => !root.IsPresent && root.Validation == null && root.Inspection.Assets.Count == 0);
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
        api.VerifyNoOtherCalls();
    }

    /// <summary>Inspection and validation retain one generation until native work and its file readers finish.</summary>
    /// <returns>The asynchronous publication/inspection race regression.</returns>
    [TestMethod]
    public async Task InspectionHoldsItsFilesWithoutBlockingThePublicationGate()
    {
        using var workspace = new CookWorkspace();
        await workspace.SeedEmptyPublicationAsync("Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = workspace.CookedRoot("Content");
        Directory.CreateDirectory(root);
        var index = Path.Combine(root, "container.index.bin");
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var api = new Mock<IEngineContentPipelineApi>();
        _ = api.Setup(value => value.ReadInventoryAsync(root, It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>())).Returns(async () =>
        {
            var write = () => File.WriteAllText(index, "replace");
            _ = write.Should().Throw<IOException>();
            entered.SetResult();
            await release.Task.ConfigureAwait(false);
            return NativeInventoryFixture.Read(root);
        });
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator([]), api.Object);
        var inspection = pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken, validate: true);
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            var writer = CookOutputLease.AcquireWriteAsync(workspace.Root, this.TestContext.CancellationToken);
            using var publication = await writer.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            release.SetResult();
            _ = (await inspection.ConfigureAwait(false)).Roots.Single().Validation!.Succeeded.Should().BeTrue();
            await File.WriteAllTextAsync(index, "new generation", this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _ = release.TrySetResult();
            _ = await inspection.ConfigureAwait(false);
        }
    }

    /// <summary>Project replacement cancels a pending read and releases the inspected file handles.</summary>
    /// <returns>The asynchronous inspection-lifetime regression.</returns>
    [TestMethod]
    public async Task ProjectClosureCancelsInspectionAndReleasesItsReaders()
    {
        using var workspace = new CookWorkspace();
        await workspace.SeedEmptyPublicationAsync("Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = workspace.CookedRoot("Content");
        var index = Path.Combine(root, "container.index.bin");
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var api = new Mock<IEngineContentPipelineApi>();
        _ = api.Setup(value => value.ReadInventoryAsync(root, It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>())).Returns(async (string _, NativeArtifactLease? _, CancellationToken token) =>
        {
            entered.SetResult();
            await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
            return NativeInventoryFixture.Read(root);
        });
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator([]), api.Object);
        var inspection = pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.ContextService.Close();
        var wait = () => inspection;
        _ = await wait.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        await File.WriteAllTextAsync(index, "reader released", this.TestContext.CancellationToken).ConfigureAwait(false);
        var obsolete = () => pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken, expectedProject: workspace.ProjectContext);
        _ = await obsolete.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        api.Verify(value => value.ReadInventoryAsync(root, It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()), Times.Once);
    }

    /// <summary>A local cooked mount resolves to its declared physical root and scopes by native descriptor location.</summary>
    /// <returns>The asynchronous local-root regression.</returns>
    [TestMethod]
    public async Task LocalInspectionUsesTheDeclaredRootWithoutInventingSourceOwnership()
    {
        using var workspace = new CookWorkspace();
        var root = Path.Combine(workspace.Root, "ExternalLibrary");
        Directory.CreateDirectory(root);
        var project = workspace.ProjectContext with { LocalFolderMounts = [new("Library", root)] };
        workspace.Activate(project);
        var api = new Mock<IEngineContentPipelineApi>();
        CookedAssetEntry[] entries =
        [
            new("/Foreign/Mesh.ogeo", ContentCookAssetKind.Geometry) { DescriptorRelativePath = "Geometry/Mesh.ogeo" },
            new("/Foreign/Blue.omat", ContentCookAssetKind.Material) { DescriptorRelativePath = "Materials/Blue.omat" },
        ];
        foreach (var entry in entries)
        {
            var descriptor = Path.Combine(root, entry.DescriptorRelativePath!);
            Directory.CreateDirectory(Path.GetDirectoryName(descriptor)!);
            await File.WriteAllTextAsync(descriptor, "controlled descriptor", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        NativeInventoryFixture.WriteIndex(root, entries);
        using var accepted = await workspace.Publication.AcquireForMountAsync(project, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = api.Setup(value => value.ReadInventoryAsync(root, It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(() => NativeInventoryFixture.Read(root));
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator([]), api.Object);
        var report = await pipeline.InspectCookedOutputAsync(new("asset:///Library/Geometry"), this.TestContext.CancellationToken, expectedProject: project).ConfigureAwait(false);
        var inspected = report.Roots.Should().ContainSingle().Subject;
        _ = inspected.Inspection.CookedRoot.Should().Be(root);
        _ = inspected.Inspection.Assets.Should().ContainSingle().Which.Kind.Should().Be(ContentCookAssetKind.Geometry);
        _ = inspected.Provenance.Should().BeEmpty();
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
    }
}
