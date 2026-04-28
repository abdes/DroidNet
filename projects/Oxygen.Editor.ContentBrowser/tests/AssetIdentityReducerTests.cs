// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class AssetIdentityReducerTests
{
    [TestMethod]
    public void Reduce_WhenDescriptorAndCookedExist_ShouldMergeAsDescriptorWithCookedDerivedState()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Red.omat.json");
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/Red.omat");
        WriteMaterial(descriptorPath);
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        File.WriteAllBytes(cookedPath, [1]);
        File.SetLastWriteTimeUtc(descriptorPath, DateTime.UtcNow.AddMinutes(-5));
        File.SetLastWriteTimeUtc(cookedPath, DateTime.UtcNow);

        var rows = Reduce(
            workspace,
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat")),
            ]);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.AreEqual(AssetState.Cooked, rows[0].DerivedState);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Red.omat.json"), rows[0].IdentityUri);
    }

    [TestMethod]
    public void Reduce_WhenProjectRootCatalogDuplicatesAuthoringMount_ShouldIgnoreProjectRootDuplicate()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));

        var rows = Reduce(
            workspace,
            [
                new AssetRecord(new Uri("asset:///project/Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
            ]);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Red.omat.json"), rows[0].IdentityUri);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
    }

    [TestMethod]
    public void Reduce_WhenImportSidecarExistsNextToMaterial_ShouldNotCreateSidecarRow()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        File.WriteAllText(workspace.SourcePath("Content/Materials/Red.omat.json.import.json"), "{}");

        var rows = Reduce(
            workspace,
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json.import.json")),
            ]);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Red.omat.json"), rows[0].IdentityUri);
        Assert.AreEqual(AssetKind.Material, rows[0].Kind);
    }

    [TestMethod]
    public void Reduce_WhenOnlyImportSidecarExists_ShouldNotPublishBrowsableRow()
    {
        using var workspace = new TempWorkspace();
        var sidecarPath = workspace.SourcePath("Content/Materials/Red.omat.json.import.json");
        Directory.CreateDirectory(Path.GetDirectoryName(sidecarPath)!);
        File.WriteAllText(sidecarPath, "{}");

        var rows = Reduce(workspace, [new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json.import.json"))]);

        Assert.AreEqual(0, rows.Count);
    }

    [TestMethod]
    public void Reduce_WhenCookedFolderIsExposedAsMount_ShouldIgnoreDerivedRootRows()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/Red.omat");
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        File.WriteAllBytes(cookedPath, [1]);

        var rows = Reduce(
            workspace,
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat")),
                new AssetRecord(new Uri("asset:///Cooked/Content/Materials/Red.omat")),
            ]);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(new Uri("asset:///Content/Materials/Red.omat.json"), rows[0].IdentityUri);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.AreEqual(AssetState.Cooked, rows[0].DerivedState);
        CollectionAssert.DoesNotContain(rows[0].DiagnosticCodes.ToList(), AssetIdentityDiagnosticCodes.CookedMissing);
    }

    [TestMethod]
    public void Reduce_WhenDescriptorIsNewerThanCooked_ShouldMarkDerivedStateStale()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Gold.omat.json");
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/Gold.omat");
        WriteMaterial(descriptorPath);
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        File.WriteAllBytes(cookedPath, [1]);
        File.SetLastWriteTimeUtc(cookedPath, DateTime.UtcNow.AddMinutes(-5));
        File.SetLastWriteTimeUtc(descriptorPath, DateTime.UtcNow);

        var rows = Reduce(
            workspace,
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Gold.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Gold.omat")),
            ]);

        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.AreEqual(AssetState.Stale, rows[0].DerivedState);
    }

    [TestMethod]
    public void Reduce_ShouldResolveCookedPathsFromProjectCookScope()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Red.omat.json");
        var cookedPath = workspace.SourcePath("custom-cooked/Content/Materials/Red.omat");
        WriteMaterial(descriptorPath);
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        File.WriteAllBytes(cookedPath, [1]);

        var project = CreateProjectContext(workspace);
        var rows = new AssetIdentityReducer().Reduce(
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat")),
            ],
            project,
            new ProjectCookScope(project.ProjectId, project.ProjectRoot, workspace.SourcePath("custom-cooked")),
            AssetBrowserFilter.Default);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(cookedPath, rows[0].CookedPath);
        Assert.AreEqual(AssetState.Cooked, rows[0].DerivedState);
    }

    [TestMethod]
    public void Reduce_WhenMergedRowPrimaryIsFilteredOut_ShouldStillMatchDerivedState()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Red.omat.json");
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/Red.omat");
        WriteMaterial(descriptorPath);
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        File.WriteAllBytes(cookedPath, [1]);
        File.SetLastWriteTimeUtc(descriptorPath, DateTime.UtcNow.AddMinutes(-5));
        File.SetLastWriteTimeUtc(cookedPath, DateTime.UtcNow);

        var project = CreateProjectContext(workspace);
        var filter = AssetBrowserFilter.Default with { IncludeDescriptor = false, IncludeCooked = true };
        var rows = new AssetIdentityReducer().Reduce(
            [
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json")),
                new AssetRecord(new Uri("asset:///Content/Materials/Red.omat")),
            ],
            project,
            CreateCookScope(workspace, project),
            filter);

        Assert.AreEqual(1, rows.Count);
        Assert.AreEqual(AssetState.Descriptor, rows[0].PrimaryState);
        Assert.AreEqual(AssetState.Cooked, rows[0].DerivedState);
    }

    [TestMethod]
    public void Reduce_WhenDescriptorJsonIsInvalid_ShouldMarkBrokenWithDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Broken.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        File.WriteAllText(descriptorPath, "{ invalid json");

        var rows = Reduce(workspace, [new AssetRecord(new Uri("asset:///Content/Materials/Broken.omat.json"))]);

        Assert.AreEqual(AssetState.Broken, rows[0].PrimaryState);
        CollectionAssert.Contains(rows[0].DiagnosticCodes.ToList(), AssetIdentityDiagnosticCodes.DescriptorBroken);
    }

    [TestMethod]
    public void CreateMissing_ShouldPreserveAuthoredUriAndDiagnostic()
    {
        var uri = new Uri("asset:///Content/Materials/Missing.omat.json");
        var row = new AssetIdentityReducer().CreateMissing(uri);

        Assert.AreEqual(uri, row.IdentityUri);
        Assert.AreEqual(AssetState.Missing, row.PrimaryState);
        CollectionAssert.Contains(row.DiagnosticCodes.ToList(), AssetIdentityDiagnosticCodes.ResolveMissing);
    }

    private static IReadOnlyList<ContentBrowserAssetItem> Reduce(TempWorkspace workspace, IReadOnlyList<AssetRecord> records)
    {
        var project = CreateProjectContext(workspace);
        return new AssetIdentityReducer().Reduce(
            records,
            project,
            CreateCookScope(workspace, project),
            AssetBrowserFilter.Default with { IncludeMissing = true });
    }

    private static ProjectContext CreateProjectContext(TempWorkspace workspace)
    {
        return new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Test",
            Category = Category.Games,
            ProjectRoot = workspace.Root,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        };
    }

    private static ProjectCookScope CreateCookScope(TempWorkspace workspace, ProjectContext project)
        => new(project.ProjectId, project.ProjectRoot, workspace.SourcePath(".cooked"));

    private static void WriteMaterial(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(
            stream,
            new MaterialSource(
                "oxygen.material.v1",
                "PBR",
                "Material",
                new MaterialPbrMetallicRoughness(
                    1.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    metallicFactor: 0.0f,
                    roughnessFactor: 0.5f,
                    baseColorTexture: null,
                    metallicRoughnessTexture: null),
                normalTexture: null,
                occlusionTexture: null,
                alphaMode: MaterialAlphaMode.Opaque,
                alphaCutoff: 0.5f,
                doubleSided: false));
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-asset-identity-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
        }

        public string Root { get; }

        public string SourcePath(string relative)
            => Path.Combine(this.Root, relative.Replace('/', Path.DirectorySeparatorChar));

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
