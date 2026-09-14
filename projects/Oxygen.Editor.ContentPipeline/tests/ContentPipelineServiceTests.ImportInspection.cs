// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Follows retained source ownership when inspecting output across authoring mounts.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Source and source-folder inspection find the actual output mount and named native assets.</summary>
    /// <returns>The asynchronous source inspection regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportedSourceInspectionFollowsItsOutputMount()
    {
        using var workspace = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        var source = await WriteCrossMountModelAsync(workspace, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var cooked = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue();
        var expected = cooked.CookedAssets.Select(static asset => asset.VirtualPath).ToArray();
        foreach (var scope in new[] { source, new Uri("asset:///Content/SourceMedia/DCC") })
        {
            var report = await service.InspectCookedOutputAsync(scope, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = report.Roots.SelectMany(static root => root.Inspection.Assets).Select(static asset => asset.VirtualPath).Should().BeEquivalentTo(expected);
            _ = report.Roots.Single(static root => string.Equals(root.Name, "Art", StringComparison.Ordinal)).Provenance.Should().NotBeEmpty();
            _ = report.Roots.Single(static root => string.Equals(root.Name, "Art", StringComparison.Ordinal)).Provenance.Should().OnlyContain(origin => origin.SourceAssetUri == source);
        }

        _ = workspace.CookCoordinator.Runs.Should().ContainSingle();
    }

    /// <summary>An uncooked source report names its declared output root without starting a native process.</summary>
    /// <returns>The asynchronous pre-cook inspection regression.</returns>
    [TestMethod]
    public async Task UncookedImportedSourceInspectionDoesNotCookOrInspectAnotherMount()
    {
        using var workspace = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        var source = await WriteCrossMountModelAsync(workspace, this.TestContext.CancellationToken).ConfigureAwait(false);
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        var service = CreateService(workspace, Mock.Of<ISceneDescriptorGenerator>(), api.Object);
        var report = await service.InspectCookedOutputAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = report.Roots.Should().ContainSingle().Which.Name.Should().Be("Art");
        _ = report.Roots.Should().OnlyContain(static root => !root.IsPresent);
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
        api.VerifyNoOtherCalls();
    }
}
