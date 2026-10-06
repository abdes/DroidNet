// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class CookedOutputInspectionTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Native scene inspection exposes its verified source/dependencies while source edits remain independent of output integrity.</summary>
    /// <returns>The asynchronous native report regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task InspectionReportsVerifiedSceneDependenciesWithoutCooking()
    {
        using var workspace = new CookWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
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
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
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
        _ = changed.Roots.Single().Validation!.Diagnostics.Should().Contain(issue => issue.AffectedPath == path);
    }
}
