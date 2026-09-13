// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Qualifies scoped reports, provenance and finite read ownership independently of cooking.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Native scene inspection exposes its verified source/dependencies while source edits remain independent of output integrity.</summary>
    /// <returns>The asynchronous native report regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task InspectionReportsVerifiedSceneDependenciesWithoutCooking()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var before = ReadOutputIdentities(workspace.Root);
        var cookCount = workspace.CookCoordinator.Runs.Count;
        var imports = api.Imported.Count;
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Changed after publication");
        var report = await pipeline.InspectCookedOutputAsync(new("asset:///Content/Scenes"), this.TestContext.CancellationToken, validate: true, workspace.ProjectContext).ConfigureAwait(false);
        var root = report.Roots.Should().ContainSingle().Subject;
        _ = root.Inspection.Assets.Should().ContainSingle().Which.Kind.Should().Be(ContentCookAssetKind.Scene);
        _ = root.Validation!.Succeeded.Should().BeTrue();
        var scene = root.Provenance.Single(origin => origin.SourceAssetUri == new Uri("asset:///Content/Scenes/Main.oscene.json"));
        _ = scene.Dependencies.Should().NotBeEmpty();
        _ = root.Inspection.Files.Should().NotBeEmpty();
        _ = api.Imported.Should().HaveCount(imports);
        _ = workspace.CookCoordinator.Runs.Should().HaveCount(cookCount);
        _ = ReadOutputIdentities(workspace.Root).Should().BeEquivalentTo(before);
    }

    /// <summary>A changed descriptor cannot inherit cached authoring ownership from its previous bytes.</summary>
    /// <returns>The asynchronous provenance regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task InspectionRejectsProvenanceForChangedCookedBytes()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var pipeline = CreateIncrementalService(workspace, CreateRecordingApi(compatibility), compatibility);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var first = await pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken).ConfigureAwait(false);
        var root = first.Roots.Should().ContainSingle().Subject;
        var entry = root.Inspection.Assets.Single(asset => asset.Kind == ContentCookAssetKind.Material);
        _ = root.Provenance.Should().NotBeEmpty();
        var path = Path.Combine(root.Inspection.CookedRoot, entry.DescriptorRelativePath!);
        var bytes = await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        bytes[^1] ^= 1;
        await File.WriteAllBytesAsync(path, bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        var changed = await pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken, validate: true).ConfigureAwait(false);
        _ = changed.Roots.Single().Provenance.Should().NotContain(origin => origin.CookedAssetUri.AbsolutePath == entry.VirtualPath);
        _ = changed.Roots.Single().Validation!.Succeeded.Should().BeFalse();
        _ = changed.Roots.Single().Validation!.Diagnostics.Should().Contain(issue => issue.Message.Contains("published content", StringComparison.Ordinal));
    }

    /// <summary>Project reports include every authoring mount and represent never-cooked roots without native work.</summary>
    /// <returns>The asynchronous multi-root scope regression.</returns>
    [TestMethod]
    public async Task ProjectInspectionIncludesAllOwnedRootsWithoutCookingMissingOutput()
    {
        using var workspace = new TempWorkspace([new("Cooked", ".cooked"), new("Content", "Content"), new("Extra", "MoreContent")]);
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api.Object);
        var report = await pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken, validate: true).ConfigureAwait(false);
        _ = report.Roots.Select(static root => root.Name).Should().Equal("Content", "Extra");
        _ = report.Roots.Should().OnlyContain(root => !root.IsPresent && root.Validation == null && root.Inspection.Assets.Count == 0);
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
        api.VerifyNoOtherCalls();
    }

    /// <summary>Inspection and validation retain one generation until native work and its file readers finish.</summary>
    /// <returns>The asynchronous publication/inspection race regression.</returns>
    [TestMethod]
    public async Task InspectionHoldsFilesAndDelaysPublicationUntilReadFinishes()
    {
        using var workspace = new TempWorkspace();
        var root = Path.Combine(workspace.Root, ".cooked", "Content");
        Directory.CreateDirectory(root);
        var index = Path.Combine(root, "container.index.bin");
        await File.WriteAllTextAsync(index, "index", this.TestContext.CancellationToken).ConfigureAwait(false);
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var api = new Mock<IEngineContentPipelineApi>();
        _ = api.Setup(value => value.InspectLooseCookedRootAsync(root, It.IsAny<CancellationToken>())).Returns(async () =>
        {
            var write = () => File.WriteAllText(index, "replace");
            _ = write.Should().Throw<IOException>();
            entered.SetResult();
            await release.Task.ConfigureAwait(false);
            return new CookInspectionResult(root, Succeeded: true, SourceIdentity: null, [], [], []);
        });
        _ = api.Setup(value => value.ValidateLooseCookedRootAsync(root, It.IsAny<CancellationToken>())).ReturnsAsync(new CookValidationResult(root, Succeeded: true, []));
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api.Object);
        var inspection = pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken, validate: true);
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            var writer = CookOutputLease.AcquireWriteAsync(workspace.Root, this.TestContext.CancellationToken);
            _ = writer.IsCompleted.Should().BeFalse();
            release.SetResult();
            _ = (await inspection.ConfigureAwait(false)).Roots.Single().Validation!.Succeeded.Should().BeTrue();
            using var publication = await writer.ConfigureAwait(false);
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
        using var workspace = new TempWorkspace();
        var root = Path.Combine(workspace.Root, ".cooked", "Content");
        var index = Path.Combine(root, "container.index.bin");
        await File.WriteAllTextAsync(index, "index", this.TestContext.CancellationToken).ConfigureAwait(false);
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var api = new Mock<IEngineContentPipelineApi>();
        _ = api.Setup(value => value.InspectLooseCookedRootAsync(root, It.IsAny<CancellationToken>())).Returns(async (string _, CancellationToken token) =>
        {
            entered.SetResult();
            await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
            return new CookInspectionResult(root, Succeeded: true, SourceIdentity: null, [], [], []);
        });
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api.Object);
        var inspection = pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.ContextService.Close();
        var wait = () => inspection;
        _ = await wait.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        await File.WriteAllTextAsync(index, "reader released", this.TestContext.CancellationToken).ConfigureAwait(false);
        var obsolete = () => pipeline.InspectCookedOutputAsync(scopeUri: null, this.TestContext.CancellationToken, expectedProject: workspace.ProjectContext);
        _ = await obsolete.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        api.Verify(value => value.InspectLooseCookedRootAsync(root, It.IsAny<CancellationToken>()), Times.Once);
    }

    /// <summary>A local cooked mount resolves to its declared physical root and scopes by native descriptor location.</summary>
    /// <returns>The asynchronous local-root regression.</returns>
    [TestMethod]
    public async Task LocalInspectionUsesTheDeclaredRootWithoutInventingSourceOwnership()
    {
        using var workspace = new TempWorkspace();
        var root = Path.Combine(workspace.Root, "ExternalLibrary");
        Directory.CreateDirectory(root);
        await File.WriteAllTextAsync(Path.Combine(root, "container.index.bin"), "index", this.TestContext.CancellationToken).ConfigureAwait(false);
        var project = workspace.ProjectContext with { LocalFolderMounts = [new("Library", root)] };
        workspace.ContextService.Activate(project);
        var api = new Mock<IEngineContentPipelineApi>();
        CookedAssetEntry[] entries =
        [
            new("/Foreign/Mesh.ogeo", ContentCookAssetKind.Geometry) { DescriptorRelativePath = "Geometry/Mesh.ogeo" },
            new("/Foreign/Blue.omat", ContentCookAssetKind.Material) { DescriptorRelativePath = "Materials/Blue.omat" },
        ];
        _ = api.Setup(value => value.InspectLooseCookedRootAsync(root, It.IsAny<CancellationToken>())).ReturnsAsync(new CookInspectionResult(root, Succeeded: true, SourceIdentity: Guid.NewGuid(), entries, [], []));
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api.Object);
        var report = await pipeline.InspectCookedOutputAsync(new("asset:///Library/Geometry"), this.TestContext.CancellationToken, expectedProject: project).ConfigureAwait(false);
        var inspected = report.Roots.Should().ContainSingle().Subject;
        _ = inspected.Inspection.CookedRoot.Should().Be(root);
        _ = inspected.Inspection.Assets.Should().ContainSingle().Which.Kind.Should().Be(ContentCookAssetKind.Geometry);
        _ = inspected.Provenance.Should().BeEmpty();
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
    }
}
