// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class UnsavedCookInputsTests
{
    private const string MaterialA = "Content/Materials/A.omat.json";
    private const string MaterialB = "Content/Other/B.omat.json";

    private const string Geometry = """
        { "name": "Box", "virtual_path": "/Content/Geometry/Box.ogeo",
          "buffers": [ { "uri": "mesh.bin", "virtual_path": "/Content/Geometry/Box.obuf" } ],
          "lods": [ { "submeshes": [ { "slot_id": "a", "material_ref": "/Content/Materials/A.omat", "views": [] } ] } ] }
        """;

    private const string Scene = """
        { "Id": "6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10", "Name": "Main",
          "RootNodes": [ { "Name": "Box", "Components": [ { "$type": "GeometryComponent",
            "GeometryUri": "asset:///Content/Geometry/Box.ogeo" } ] } ] }
        """;

    public TestContext TestContext { get; set; } = null!;

    /// <summary>A scene cook reads the unsaved material the scene uses, and not an unrelated unsaved one.</summary>
    /// <param name="kind">The scene's cook, as a current-scene or an asset request.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Asset)]
    public async Task FindAsync_Scene_ReturnsTheUnsavedMaterialItUses(CookTargetKind kind)
    {
        using var workspace = CreateProject();
        var (a, _) = MarkUnsaved(workspace, workspace.FileOf(MaterialA), "/Content/Materials/A.omat", workspace.FileOf(MaterialB), "/Content/Other/B.omat");

        var found = await CreateInputs(workspace).FindAsync(new(kind, AssetPath.Parse("/Content/Scenes/Main.oscene")), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().Equal(a);
    }

    /// <summary>A cook of one asset reads that asset only.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindAsync_Asset_ReturnsOnlyTheUnsavedDocumentOfThatAsset()
    {
        using var workspace = CreateProject();
        var (_, b) = MarkUnsaved(workspace, workspace.FileOf(MaterialA), "/Content/Materials/A.omat", workspace.FileOf(MaterialB), "/Content/Other/B.omat");

        var found = await CreateInputs(workspace).FindAsync(new(CookTargetKind.Asset, AssetPath.Parse("/Content/Other/B.omat")), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().Equal(b);
    }

    /// <summary>A cook of a folder reads the unsaved documents inside it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindAsync_Folder_ReturnsTheUnsavedDocumentsInsideIt()
    {
        using var workspace = CreateProject();
        var (_, b) = MarkUnsaved(workspace, workspace.FileOf(MaterialA), "/Content/Materials/A.omat", workspace.FileOf(MaterialB), "/Content/Other/B.omat");

        var found = await CreateInputs(workspace).FindAsync(new(CookTargetKind.Folder, AssetPath.Parse("/Content/Other")), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Should().Equal(b);
    }

    /// <summary>A cook of the project reads every unsaved document in it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindAsync_Project_ReturnsEveryUnsavedDocument()
    {
        using var workspace = CreateProject();
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
        using var workspace = CreateProject();
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

    private static RelocationWorkspace CreateProject()
    {
        var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Scenes/Main.oscene.json", Scene);
        _ = workspace.Write("Content/Geometry/Box.ogeo.json", Geometry);
        _ = workspace.Write("Content/Geometry/mesh.bin", "bin");
        _ = workspace.Write(MaterialA, """{ "name": "A" }""");
        _ = workspace.Write(MaterialB, """{ "name": "B" }""");
        return workspace;
    }
}
