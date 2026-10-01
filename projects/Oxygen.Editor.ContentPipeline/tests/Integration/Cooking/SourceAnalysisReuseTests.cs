// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class SourceAnalysisReuseTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>A newly changed scene and a cached scene repair share one generated builtin source.</summary>
    /// <returns>The mixed-frontier native verification.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task MixedFreshAndCachedSceneRepairSharesBuiltinDescriptors()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        var second = new Scene(workspace.Project) { Name = "Second" };
        var node = new SceneNode(second) { Name = "OtherCube" };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        });
        second.RootNodes.Add(node);
        var source = File.Create(Path.Combine(workspace.Root, "Content", "Scenes", "Second.oscene.json"));
        await using (source.ConfigureAwait(false))
        {
            await new Oxygen.Editor.World.Serialization.SceneSerializer(workspace.Project).SerializeAsync(source, second).ConfigureAwait(false);
        }

        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var descriptor = first.Inspection!.Assets.Single(static asset => string.Equals(asset.VirtualPath, "/Content/Scenes/Second.oscene", StringComparison.Ordinal));
        await File.WriteAllBytesAsync(Path.Combine(first.Inspection.CookedRoot, descriptor.DescriptorRelativePath!), [0], this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.Scene.RootNodes[0].Name = "FreshlyChanged";
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        api.Imported.Clear();
        api.Analyzed.Clear();

        var repaired = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(repaired);
        _ = api.Analyzed.SelectMany(static execution => execution.Jobs).Should().NotContain(job => job.Source.Contains("Second", StringComparison.Ordinal));
        _ = api.Imported.SelectMany(static execution => execution.Manifest.Jobs).Where(static job => string.Equals(job.Type, "geometry-descriptor", StringComparison.Ordinal))
            .Should().ContainSingle();
    }

    /// <summary>Repairing an unchanged scene's output does not launch another source-analysis worker.</summary>
    /// <returns>The native repair verification.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CorruptSceneDescriptorReusesSourceFacts()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = CreateRecordingApi(compatibility);
        var pipeline = CreateIncrementalService(workspace, api, compatibility);
        var first = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var descriptor = first.Inspection!.Assets.Single(static asset => asset.Kind == ContentCookAssetKind.Scene);
        var path = Path.Combine(first.Inspection.CookedRoot, descriptor.DescriptorRelativePath!);
        var expected = await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllBytesAsync(path, [0], this.TestContext.CancellationToken).ConfigureAwait(false);
        var beforeAnalysis = api.Analyzed.Count;

        var repaired = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(repaired);
        _ = api.Analyzed.Should().HaveCount(beforeAnalysis);
        _ = (await File.ReadAllBytesAsync(Path.Combine(workspace.CookedRoot("Content"), descriptor.DescriptorRelativePath!), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().Equal(expected);
        _ = (await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }

    /// <summary>Deferred scene projection uses captured authoring even if the live scene changed before repair.</summary>
    /// <returns>The captured-input verification.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task DeferredSceneProjectionUsesCapturedBytes()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        const string relative = "Content/Scenes/Main.oscene.json";
        workspace.WriteText(".build/captured/" + relative, workspace.ReadText(relative));
        workspace.Scene.RootNodes[0].Name = "EditedAfterCapture";
        await workspace.WriteSceneAsync(relative).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var operation = new ContentCookOperation(Guid.NewGuid(), workspace.ProjectContext, 1);
        var verified = await compatibility.VerifyAsync(operation.OperationId, this.TestContext.CancellationToken).ConfigureAwait(false);
        var artifacts = verified.Artifacts ?? throw new InvalidOperationException("The native test artifacts are unavailable.");
        await using var lifetime = artifacts.ConfigureAwait(false);
        var api = CreateRecordingApi(compatibility);
        var analyzer = new CookSourceAnalyzer(
            operation,
            artifacts,
            api,
            new ContentImportManifestBuilder(),
            new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)),
            new FixedCookScopeProvider(workspace.Root),
            new CookDocumentRegistry());
        var input = CookInputResolver.Resolve(workspace.ProjectContext, new Uri("asset:///Content/Scenes/Main.oscene.json"), ContentCookInputRole.Primary);
        var snapshot = new CookInputSnapshot(operation, Path.Combine(workspace.Root, ".build", "captured"), artifacts.Fingerprint, "captured-scene-test", [], []);

        var prepared = await analyzer.PrepareCapturedScenesAsync([input], snapshot, [], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = prepared.Diagnostics.Should().BeEmpty();
        var descriptor = await File.ReadAllTextAsync(prepared.Sources.Single().Scene!.DescriptorPath, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = descriptor.Should().Contain("\"Cube\"").And.NotContain("EditedAfterCapture");
        _ = api.Analyzed.Should().BeEmpty();
        _ = prepared.GeneratedInputs.Should().NotBeEmpty();
    }
}
