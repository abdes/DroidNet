// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.SavedSourceScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ChangedSourcesCookTests
{
    private static readonly AssetPath Red = AssetPath.Parse("/Content/Materials/Red.omat");
    private static readonly AssetPath Blue = AssetPath.Parse("/Content/Materials/Blue.omat");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Several changed sources cook in one automatic run, named by how many there are.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SeveralChangedSourcesCookAsOneRun()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        var api = CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync);
        var service = CreateService(workspace, api);

        _ = await service.CookChangedSourcesAsync(
            new CookChanges { Sources = [AssetPath.Parse("/Content/Materials/Red.omat.json"), AssetPath.Parse("/Content/Materials/Blue.omat.json")] },
            workspace.ProjectContext,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        var run = workspace.CookCoordinator.Runs.Should().ContainSingle().Which;
        _ = run.Request.TargetKind.Should().Be(CookTargetKind.Changes);
        _ = run.Request.IsAutomatic.Should().BeTrue();
        _ = run.Request.Assets.Should().BeEquivalentTo([Red, Blue]);
        _ = run.DisplayName.Should().Be("2 changed assets");
        _ = api.ImportedManifest!.Jobs.Select(static job => job.VirtualPath).Should().Contain([Red.VirtualPath, Blue.VirtualPath]);
    }

    /// <summary>One changed source cooks as that asset's automatic run.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task OneChangedSourceCooksAsItsAssetsRun()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var service = CreateService(workspace, CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync));

        _ = await service.CookChangedSourcesAsync(
            new CookChanges { Sources = [AssetPath.Parse("/Content/Materials/Red.omat.json")] },
            workspace.ProjectContext,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        var run = workspace.CookCoordinator.Runs.Should().ContainSingle().Which;
        _ = run.Request.TargetKind.Should().Be(CookTargetKind.Asset);
        _ = run.Request.ScopeUri.Should().Be(Red);
        _ = run.Request.IsAutomatic.Should().BeTrue();
    }

    /// <summary>Each source left out for its errors, or those of a source it uses, is named in the run that cooks the others.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SkippedSourcesAreNamedInTheRun()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var service = CreateService(workspace, CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync));

        _ = await service.CookChangedSourcesAsync(
            new CookChanges
            {
                Sources = [AssetPath.Parse("/Content/Materials/Red.omat.json")],
                Skipped =
                [
                    new SkippedCookSource { Source = AssetPath.Parse("/Content/Scripts/Orbit.lua"), ErrorCount = 2 },
                    new SkippedCookSource
                    {
                        Source = AssetPath.Parse("/Content/Scenes/Main.oscene.json"),
                        ErrorCount = 2,
                        Dependencies = [AssetPath.Parse("/Content/Scripts/Orbit.lua")],
                    },
                ],
            },
            workspace.ProjectContext,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        var skipped = workspace.CookCoordinator.Runs.Single().Diagnostics.Where(static issue => issue.Code == ContentPipelineService.SkippedForProblemsCode).ToList();
        _ = skipped.Should().OnlyContain(static issue => issue.Severity == DiagnosticSeverity.Info);
        _ = skipped.Select(static issue => issue.AffectedVirtualPath).Should().Equal("/Content/Scripts/Orbit.oscript", "/Content/Scenes/Main.oscene");
    }

    /// <summary>A source moved or deleted before its run starts leaves the run instead of failing it.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SourceGoneBeforeTheRunLeavesIt()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync);
        var service = CreateService(workspace, api);

        var result = await service.CookChangedSourcesAsync(
            new CookChanges { Sources = [AssetPath.Parse("/Content/Materials/Red.omat.json"), AssetPath.Parse("/Content/Materials/Blue.omat.json")] },
            workspace.ProjectContext,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Diagnostics.Should().NotContain(issue => issue.Code == AssetImportDiagnosticCodes.SourceMissing);
        _ = api.ImportedManifest!.Jobs.Select(static job => job.VirtualPath).Should().Contain(Red.VirtualPath).And.NotContain(Blue.VirtualPath);
    }

    /// <summary>A changed script cooks on its own, as a script input named by its identity.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ChangedScriptCooksOnItsOwn()
    {
        using var workspace = new CookWorkspace();
        var script = Path.Combine(workspace.Root, "Content", "Scripts", "Orbit.lua");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(script)!);
        await File.WriteAllTextAsync(script, "return {}\n", this.TestContext.CancellationToken).ConfigureAwait(false);
        var api = CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync);
        var service = CreateService(workspace, api);

        var result = await service.CookChangedSourcesAsync(
            new CookChanges { Sources = [AssetPath.Parse("/Content/Scripts/Orbit.lua")] },
            workspace.ProjectContext,
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = api.ImportedManifest.Should().NotBeNull("the cook reached its import; it reported {0}", string.Join("; ", result.Diagnostics.Select(static issue => issue.Code)));
        _ = api.ImportedManifest!.Jobs.Should().ContainSingle(job => job.Type == "script" && job.VirtualPath == "/Content/Scripts/Orbit.oscript");
    }

    /// <summary>Retrying several sources is one requested run of them.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SeveralAssetsCookAsOneRequestedRun()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
        var service = CreateService(workspace, CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync));

        _ = await service.CookAssetsAsync([Red, Blue], this.TestContext.CancellationToken).ConfigureAwait(false);

        var run = workspace.CookCoordinator.Runs.Should().ContainSingle().Which;
        _ = run.Request.TargetKind.Should().Be(CookTargetKind.Changes);
        _ = run.Request.IsAutomatic.Should().BeFalse();
        _ = run.Request.Assets.Should().BeEquivalentTo([Red, Blue]);
    }
}
