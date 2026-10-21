// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

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
                AssetPath.Parse("/Content/Materials/Wood.omat"),
                ProjectRoot: projects.ActiveProject!.ProjectRoot,
                MountName: string.Empty,
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
        var request = new MaterialCookRequest(AssetPath.Parse("/Content/Materials/Wood.omat"), projects.ActiveProject!.ProjectRoot, "Content", "Content/Materials/Wood.omat.json");
        request = mismatch switch
        {
            "project" => request with { ProjectRoot = FileSystemPath.From(projects.ActiveProject!.ProjectRoot.FullPath + "-other") },
            "mount" => request with { MountName = "Other" },
            "source" => request with { SourceRelativePath = "Content/Materials/Other.omat.json" },
            _ => request with { Material = AssetPath.Parse("/Content/Geometry/Cube.ogeo") },
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
        var uri = AssetPath.Parse("/Content/Material.omat");
        var source = projects.ActiveProject!.ProjectRoot.Combine("Content/Material.omat.json");
        var state = new AssetCookStatus(
            uri,
            freshness,
            published,
            verified ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing,
            [],
            [])
        {
            UnsavedDocuments = dirty ? [new UnsavedDocument(DocumentId.New(), new DocumentFile(source, uri), "Material")] : [],
        };
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        _ = pipeline.Setup(service => service.ReadAsync(projects.ActiveProject!, It.Is<IReadOnlyList<AssetPath>>(uris => uris.Count == 1 && uris[0] == uri), It.IsAny<CancellationToken>()))
            .ReturnsAsync([state]);
        var service = new MaterialCookService(pipeline.Object, projects, NullLogger<MaterialCookService>.Instance);
        var result = await service.GetMaterialCookStateAsync(uri, CancellationToken.None).ConfigureAwait(false);
        _ = result.Should().Be(expected);
        pipeline.VerifyAll();
    }

    private static ProjectContextService CreateProjectContext()
    {
        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new MockFileSystem()).Context with { Name = "Materials" });
        return projects;
    }
}
