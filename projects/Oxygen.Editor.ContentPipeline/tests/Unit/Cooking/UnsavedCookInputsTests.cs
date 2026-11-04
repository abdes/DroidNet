// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class UnsavedCookInputsTests
{
    private const string MaterialA = "Content/Materials/A.omat.json";
    private const string MaterialB = "Content/Other/B.omat.json";

    public TestContext TestContext { get; set; } = null!;

    /// <summary>A scene cook reads the unsaved material the scene uses, and not an unrelated unsaved one.</summary>
    /// <param name="kind">The scene's cook, as a current-scene or an asset request.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Asset)]
    public async Task FindAsync_Scene_ReturnsTheUnsavedMaterialItUses(CookTargetKind kind)
    {
        using var workspace = await CreateProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var (a, _) = MarkUnsaved(workspace, workspace.FileOf(MaterialA), "/Content/Materials/A.omat", workspace.FileOf(MaterialB), "/Content/Other/B.omat");

        var found = await CreateInputs(workspace).FindAsync(new(kind, AssetPath.Parse("/Content/Scenes/Main.oscene")), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().Equal(a);
    }

    /// <summary>A cook of one asset reads that asset only.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindAsync_Asset_ReturnsOnlyTheUnsavedDocumentOfThatAsset()
    {
        using var workspace = await CreateProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var (_, b) = MarkUnsaved(workspace, workspace.FileOf(MaterialA), "/Content/Materials/A.omat", workspace.FileOf(MaterialB), "/Content/Other/B.omat");

        var found = await CreateInputs(workspace).FindAsync(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Other/B.omat")), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().Equal(b);
    }

    /// <summary>A cook of a folder reads the unsaved documents inside it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindAsync_Folder_ReturnsTheUnsavedDocumentsInsideIt()
    {
        using var workspace = await CreateProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var (_, b) = MarkUnsaved(workspace, workspace.FileOf(MaterialA), "/Content/Materials/A.omat", workspace.FileOf(MaterialB), "/Content/Other/B.omat");

        var found = await CreateInputs(workspace).FindAsync(new(CookTargetKind.Folder, AssetPath.Parse("/Content/Other")), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().Equal(b);
    }

    /// <summary>A cook of the project reads every unsaved document in it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindAsync_Project_ReturnsEveryUnsavedDocument()
    {
        using var workspace = await CreateProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var (a, b) = MarkUnsaved(workspace, workspace.FileOf(MaterialA), "/Content/Materials/A.omat", workspace.FileOf(MaterialB), "/Content/Other/B.omat");

        var found = await CreateInputs(workspace).FindAsync(new(CookTargetKind.Project, ScopeUri: null), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().BeEquivalentTo([a, b]);
    }

    /// <summary>A cook reads no unsaved document when none is open with edits.</summary>
    /// <param name="kind">The kind of cook.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Folder)]
    [DataRow(CookTargetKind.Project)]
    public async Task FindAsync_WithoutUnsavedDocuments_ReturnsNone(CookTargetKind kind)
    {
        using var workspace = await CreateProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var scope = kind == CookTargetKind.Project ? null : AssetPath.Parse(kind == CookTargetKind.Folder ? "/Content/Other" : "/Content/Scenes/Main.oscene");

        var found = await CreateInputs(workspace).FindAsync(new(kind, scope), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().BeEmpty();
    }

    private static UnsavedCookInputs CreateInputs(RelocationWorkspace workspace) => new(workspace.Session, workspace.UnsavedDocuments.Object);

    // The documents answer Find as the open documents do: those whose file is one of the paths or inside one.
    private static (UnsavedDocument A, UnsavedDocument B) MarkUnsaved(RelocationWorkspace workspace, FileSystemPath fileA, string assetA, FileSystemPath fileB, string assetB)
    {
        var a = new UnsavedDocument(DocumentId.New(), new(fileA, AssetPath.Parse(assetA)), "A");
        var b = new UnsavedDocument(DocumentId.New(), new(fileB, AssetPath.Parse(assetB)), "B");
        UnsavedDocument[] unsaved = [a, b];
        _ = workspace.UnsavedDocuments.Setup(documents => documents.Find(It.IsAny<IEnumerable<FileSystemPath>>()))
            .Returns((IEnumerable<FileSystemPath> paths) => [.. unsaved.Where(document => paths.Any(document.File.Path.IsSameOrInside))]);
        return (a, b);
    }

    private static async Task<RelocationWorkspace> CreateProjectAsync(CancellationToken token)
    {
        var workspace = new RelocationWorkspace();
        var scene = new Scene { Name = "Main", Id = SceneId.FromValue(Guid.Parse("6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10")) };
        var box = new SceneNode(scene, "Box") { Id = new NodeId(Guid.Parse("3d9a2b1c-0000-4000-8000-000000000001")) };
        _ = box.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = AssetPath.Parse("/Content/Geometry/Box.ogeo") });
        scene.RootNodes.Add(box);
        _ = await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json", scene, token).ConfigureAwait(false);
        var geometry = AuthoredFiles.Geometry("/Content/Geometry/Box.ogeo").SubmeshMaterial(0, 0, "/Content/Materials/A.omat").WithIdentityField();
        _ = workspace.Write("Content/Geometry/Box.ogeo.json", geometry.Append("/buffers", new JsonObject { ["source"] = "mesh.bin", ["virtual_path"] = geometry.Spell("/Content/Geometry/Box.obuf") }).Text);
        _ = workspace.Write("Content/Geometry/mesh.bin", "bin");
        _ = workspace.Write(MaterialA, AuthoredFiles.Material("/Content/Materials/A.omat").Text);
        _ = workspace.Write(MaterialB, AuthoredFiles.Material("/Content/Other/B.omat").Text);
        return workspace;
    }
}
