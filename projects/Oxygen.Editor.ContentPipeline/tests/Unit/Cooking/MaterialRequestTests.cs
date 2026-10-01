// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class MaterialRequestTests
{
    /// <summary>Verifies the workflow can reject.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookMaterialAsync_WhenRequestMissingProjectFacts_ShouldReject()
    {
        var projects = CreateProjectContext();
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        var service = new MaterialCookService(pipeline.Object, projects, NullLogger<MaterialCookService>.Instance);

        var result = await service.CookMaterialAsync(
            new MaterialCookRequest(
                new Uri("asset:///Content/Materials/Wood.omat.json"),
                ProjectRoot: string.Empty,
                MountName: "Content",
                SourceRelativePath: "Content/Materials/Wood.omat.json"),
            CancellationToken.None).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Rejected);
        pipeline.VerifyNoOtherCalls();
    }

    /// <summary>Inconsistent project facts cannot target another source or create a cook run.</summary>
    /// <param name="mismatch">The mismatched request field.</param>
    /// <returns>The asynchronous validation test.</returns>
    [TestMethod]
    [DataRow("project")]
    [DataRow("mount")]
    [DataRow("source")]
    [DataRow("kind")]
    public async Task MaterialRequestMustMatchItsActiveProjectIdentity(string mismatch)
    {
        var projects = CreateProjectContext();
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        var request = new MaterialCookRequest(new("asset:///Content/Materials/Wood.omat.json"), projects.ActiveProject!.ProjectRoot, "Content", "Content/Materials/Wood.omat.json");
        request = mismatch switch
        {
            "project" => request with { ProjectRoot = projects.ActiveProject!.ProjectRoot + "-other" },
            "mount" => request with { MountName = "Other" },
            "source" => request with { SourceRelativePath = "Content/Materials/Other.omat.json" },
            _ => request with { MaterialSourceUri = new("asset:///Content/Geometry/Cube.ogeo.json") },
        };

        var service = new MaterialCookService(pipeline.Object, projects, NullLogger<MaterialCookService>.Instance);
        var result = await service.CookMaterialAsync(request, CancellationToken.None).ConfigureAwait(false);
        _ = result.State.Should().Be(MaterialCookState.Rejected);
        pipeline.VerifyNoOtherCalls();
    }

    /// <summary>Opening a material preserves its published state and keeps unsaved input out of Current.</summary>
    /// <param name="freshness">The saved-input state.</param>
    /// <param name="published">Whether prior output is known.</param>
    /// <param name="verified">Whether prior output is intact.</param>
    /// <param name="dirty">Whether the source owner has unsaved edits.</param>
    /// <param name="expected">The material document's cook state.</param>
    /// <returns>The asynchronous document-state regression.</returns>
    [TestMethod]
    [DataRow(AssetCookFreshness.Current, true, true, false, MaterialCookState.Cooked)]
    [DataRow(AssetCookFreshness.Current, true, true, true, MaterialCookState.Stale)]
    [DataRow(AssetCookFreshness.OutOfDate, true, true, false, MaterialCookState.Stale)]
    [DataRow(AssetCookFreshness.OutOfDate, true, false, false, MaterialCookState.NotCooked)]
    [DataRow(AssetCookFreshness.InvalidSource, true, true, false, MaterialCookState.Failed)]
    [DataRow(AssetCookFreshness.NeedsCooking, false, false, false, MaterialCookState.NotCooked)]
    public async Task ReadMaterialStateUsesSharedCookFacts(AssetCookFreshness freshness, bool published, bool verified, bool dirty, MaterialCookState expected)
    {
        var projects = CreateProjectContext();
        var uri = new Uri("asset:///Content/Material.omat.json");
        var source = Path.Combine(projects.ActiveProject!.ProjectRoot, "Content", "Material.omat.json");
        var state = new AssetCookStatus(
            uri,
            freshness,
            published,
            verified ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing,
            [],
            dirty ? [new CookDocumentState(Guid.NewGuid(), source, "Material", 2, 1, IsDirty: true, new string('A', 64))] : [],
            []);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        _ = pipeline.Setup(service => service.ReadAsync(projects.ActiveProject!, It.Is<IReadOnlyList<Uri>>(uris => uris.Count == 1 && uris[0] == uri), It.IsAny<CancellationToken>()))
            .ReturnsAsync([state]);
        var service = new MaterialCookService(pipeline.Object, projects, NullLogger<MaterialCookService>.Instance);
        var result = await service.GetMaterialCookStateAsync(uri, CancellationToken.None).ConfigureAwait(false);
        _ = result.Should().Be(expected);
        pipeline.VerifyAll();
    }

    private static ProjectContextService CreateProjectContext()
    {
        var projects = new ProjectContextService();
        projects.Activate(new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            ProjectRoot = Path.Combine(Path.GetTempPath(), "MaterialRequest", Guid.NewGuid().ToString("N")),
            Name = "Materials",
            Category = Category.Games,
            AuthoringMounts = [new("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        });
        return projects;
    }
}
