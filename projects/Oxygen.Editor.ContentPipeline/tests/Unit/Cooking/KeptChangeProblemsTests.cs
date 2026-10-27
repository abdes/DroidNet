// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>
/// The cook's warnings about kept changes reach the Problems pane (scene instances 8.10): each cook of a scene replaces
/// that scene's warnings with its own, and other cook diagnostics stay in the cook's panel.
/// </summary>
[TestClass]
internal sealed class KeptChangeProblemsTests
{
    private static readonly AssetPath Harbor = AssetPath.Parse("/Content/Scenes/Harbor.oscene");
    private static readonly AssetPath Dock = AssetPath.Parse("/Content/Scenes/Dock.oscene");

    [TestMethod]
    public void ACooksKeptChangeWarnings_BecomeProblems_AndTheNextCookOfTheSceneReplacesThem()
    {
        using var problems = new KeptChangeProblems(Mock.Of<ICookRunService>());
        var raised = 0;
        problems.ProblemsChanged += (_, _) => raised++;

        problems.Take(Result([Harbor], Issue(Harbor, KeptChangeDiagnostics.InstanceOverrideUnresolved), Issue(Harbor, "scene.descriptor.reference_missing")));

        var problem = problems.Problems.Should().ContainSingle().Which.Should().BeOfType<CookProblem>().Subject;
        _ = problem.Code.Should().Be(KeptChangeDiagnostics.InstanceOverrideUnresolved);
        _ = problem.Issue.AffectedVirtualPath.Should().Be(Harbor.VirtualPath);

        problems.Take(Result([Harbor]));

        _ = problems.Problems.Should().BeEmpty("the scene cooked again without kept changes");
        _ = raised.Should().Be(2);
    }

    [TestMethod]
    public void ACookOfOneScene_KeepsTheWarningsOfAnother()
    {
        using var problems = new KeptChangeProblems(Mock.Of<ICookRunService>());
        problems.Take(Result([Harbor], Issue(Harbor, KeptChangeDiagnostics.SurfaceInvalid)));

        problems.Take(Result([Dock], Issue(Dock, KeptChangeDiagnostics.ScriptInstanceOverrideUnresolved)));

        _ = problems.Problems.Select(static problem => problem.Code).Should().BeEquivalentTo(
            [KeptChangeDiagnostics.SurfaceInvalid, KeptChangeDiagnostics.ScriptInstanceOverrideUnresolved]);
    }

    // A cook of the scenes, reporting the diagnostics.
    private static ContentCookResult Result(IReadOnlyList<AssetPath> scenes, params DiagnosticRecord[] diagnostics)
        => new(
            Guid.NewGuid(),
            CookTargetKind.CurrentScene,
            OperationStatus.SucceededWithWarnings,
            diagnostics,
            [.. scenes.Select(static scene => new ContentCookedAsset(scene, scene, ContentCookAssetKind.Scene, "Content", scene.VirtualPath))],
            Inspection: null,
            Validation: null);

    private static DiagnosticRecord Issue(AssetPath scene, string code)
        => new()
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Warning,
            Code = code,
            Message = "A change has no effect until it resolves.",
            AffectedVirtualPath = scene.VirtualPath,
        };
}
