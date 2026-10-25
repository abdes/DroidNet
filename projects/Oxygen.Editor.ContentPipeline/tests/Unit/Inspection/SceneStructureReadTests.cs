// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

/// <summary>
/// The session reads one copy of each source's structure, keyed by the cooked index it was read from (scene instances
/// 8.2), and reads none for a source that cannot be instanced or a project that is no longer open.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SceneStructureReadTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Every read from the same cooked index gets the one structure read first.</summary>
    [TestMethod]
    public void TheCacheReturnsOneCopyPerCookedIndex()
    {
        using var workspace = new CookWorkspace();
        var cache = new SceneStructureCache();
        var structure = StructureOf(Car);

        cache.Store(workspace.ProjectContext, "index-1", structure);

        _ = cache.Find(workspace.ProjectContext, Car, "index-1").Should().BeSameAs(structure);
        _ = cache.Find(workspace.ProjectContext, AssetPath.Parse("/Content/Vehicles/Truck.glb"), "index-1").Should().BeNull();
    }

    /// <summary>A publication changes the cooked index, so the next read misses and its structure replaces the old one.</summary>
    [TestMethod]
    public void APublicationReplacesTheSourcesStructure()
    {
        using var workspace = new CookWorkspace();
        var cache = new SceneStructureCache();
        cache.Store(workspace.ProjectContext, "index-1", StructureOf(Car));

        _ = cache.Find(workspace.ProjectContext, Car, "index-2").Should().BeNull("the source was republished");

        var republished = StructureOf(Car);
        cache.Store(workspace.ProjectContext, "index-2", republished);
        _ = cache.Find(workspace.ProjectContext, Car, "index-2").Should().BeSameAs(republished);
        _ = cache.Find(workspace.ProjectContext, Car, "index-1").Should().BeNull("one structure per source is kept");
    }

    /// <summary>Structures of a project that was replaced are not served to the next one.</summary>
    [TestMethod]
    public void AnotherProjectDropsTheStructuresOfTheLast()
    {
        using var workspace = new CookWorkspace();
        var cache = new SceneStructureCache();
        var original = workspace.ProjectContext;
        var replacement = original with { ProjectId = Guid.NewGuid() };
        cache.Store(original, "index-1", StructureOf(Car));

        _ = cache.Find(replacement, Car, "index-1").Should().BeNull();

        cache.Store(replacement, "index-1", StructureOf(Car));
        _ = cache.Find(original, Car, "index-1").Should().BeNull();
    }

    /// <summary>A stale project identity cannot start a structure read.</summary>
    /// <returns>The asynchronous read.</returns>
    [TestMethod]
    public async Task AReplacedProjectReadsNoStructure()
    {
        using var workspace = new CookWorkspace();
        var original = workspace.ProjectContext;
        workspace.Activate(original with { ProjectId = Guid.NewGuid() });
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        ISceneStructureProvider provider = CreateService(workspace, api.Object, new CapturingSceneDescriptorGenerator([]));

        var result = await provider.ReadAsync(original, Car, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Should().BeNull();
        api.VerifyNoOtherCalls();
    }

    /// <summary>A source that is neither a model nor a scene has no structure, and no native read starts.</summary>
    /// <returns>The asynchronous read.</returns>
    [TestMethod]
    public async Task ASourceThatCannotBeInstancedReadsNoStructure()
    {
        using var workspace = new CookWorkspace();
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        _ = api.As<ICookedSceneInspector>();
        ISceneStructureProvider provider = CreateService(workspace, api.Object, new CapturingSceneDescriptorGenerator([]));

        var result = await provider.ReadAsync(workspace.ProjectContext, AssetPath.Parse("/Content/Textures/Brick.png"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Should().BeNull();
        api.VerifyNoOtherCalls();
    }

    private static SceneStructure StructureOf(AssetPath source)
        => new(source, new StructureScene(Guid.NewGuid(), "/Content/Vehicles/Car.glb/Car.oscene"), [], [], []);
}
