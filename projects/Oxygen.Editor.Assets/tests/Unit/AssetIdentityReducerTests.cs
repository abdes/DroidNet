// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets.Unit.Tests;

[TestClass]
public sealed partial class AssetIdentityReducerTests
{
    /// <summary>Timestamp-preserving source edits must invalidate parsed-descriptor results.</summary>
    [TestMethod]
    public void DescriptorValidationCacheUsesContentInsteadOfTimestamp()
    {
        using var workspace = new TempWorkspace();
        var path = workspace.SourcePath("Content/Materials/Red.omat.json");
        WriteMaterial(path);
        var timestamp = File.GetLastWriteTimeUtc(path);
        var project = CreateProjectContext(workspace);
        var reducer = new AssetIdentityReducer();
        var records = new[] { new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json")) };
        var scope = CreateCookScope(workspace, project);
        _ = reducer.Reduce(records, project, scope).Single().PrimaryState.Should().Be(AssetState.Descriptor);
        File.WriteAllText(path, "invalid");
        File.SetLastWriteTimeUtc(path, timestamp);
        _ = reducer.Reduce(records, project, scope).Single().PrimaryState.Should().Be(AssetState.Broken);
        WriteMaterial(path);
        File.SetLastWriteTimeUtc(path, timestamp);
        _ = reducer.Reduce(records, project, scope).Single().PrimaryState.Should().Be(AssetState.Descriptor);
    }

    /// <summary>An asset of the engine content pak is read-only, selectable and keeps its engine identity.</summary>
    [TestMethod]
    public void PackagedRecordsRetainTheirEngineIdentity()
    {
        using var workspace = new TempWorkspace();
        var identity = EngineAssets.BasicShape("IcoSphere");
        var pak = FileSystemPath.From(workspace.SourcePath("engine.pak"));
        var record = new AssetRecord(identity)
        {
            Cooked = new CookedAssetMetadata(pak, DescriptorRelativePath: null, Guid.CreateVersion7(), new(1, 2), AssetType: 2, DescriptorSize: 1, DescriptorSha256: null) { VirtualPath = identity },
        };
        var rows = Reduce(workspace, [record]);
        _ = rows.Should().ContainSingle();
        var row = rows[0];
        _ = row.PrimaryState.Should().Be(AssetState.Packaged);
        _ = row.Kind.Should().Be(AssetKind.Geometry);
        _ = row.Identity.Should().Be(identity);
        _ = row.Origin.Should().Be(AssetOrigin.Engine);
        _ = row.IsSelectable.Should().BeTrue();
        _ = row.CookedMetadata!.ContainerPath.Should().Be(pak);
        _ = row.CookedPath.Should().BeNull();
        _ = row.SourcePath.Should().BeNull();
        _ = row.DescriptorPath.Should().BeNull();
        _ = row.DiagnosticCodes.Should().BeEmpty();
    }
    [TestMethod]
    public void ReduceWhenDescriptorAndOutputExistRequiresFreshnessProof()
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
                new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json")),
                IndexedOutput(AssetPath.Parse("/Content/Materials/Red.omat"), cookedPath),
            ]);

        _ = rows.Should().ContainSingle();
        _ = rows[0].PrimaryState.Should().Be(AssetState.Descriptor);
        _ = rows[0].DerivedState.Should().Be(AssetState.Stale);
        _ = rows[0].Identity.Should().Be(AssetPath.Parse("/Content/Materials/Red.omat"));
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
                new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json")),
                new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json.import.json")),
            ]);

        _ = rows.Should().ContainSingle();
        _ = rows[0].Identity.Should().Be(AssetPath.Parse("/Content/Materials/Red.omat"));
        _ = rows[0].Kind.Should().Be(AssetKind.Material);
    }

    [TestMethod]
    public void Reduce_WhenOnlyImportSidecarExists_ShouldNotPublishBrowsableRow()
    {
        using var workspace = new TempWorkspace();
        var sidecarPath = workspace.SourcePath("Content/Materials/Red.omat.json.import.json");
        Directory.CreateDirectory(Path.GetDirectoryName(sidecarPath)!);
        File.WriteAllText(sidecarPath, "{}");

        var rows = Reduce(workspace, [new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json.import.json"))]);

        _ = rows.Should().BeEmpty();
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
                new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json")),
                IndexedOutput(AssetPath.Parse("/Content/Materials/Red.omat"), cookedPath),
                new AssetRecord(AssetPath.Parse("/Cooked/Content/Materials/Red.omat")),
            ]);

        _ = rows.Should().ContainSingle();
        _ = rows[0].Identity.Should().Be(AssetPath.Parse("/Content/Materials/Red.omat"));
        _ = rows[0].PrimaryState.Should().Be(AssetState.Descriptor);
        _ = rows[0].DerivedState.Should().Be(AssetState.Stale);
        _ = rows[0].DiagnosticCodes.Should().NotContain(AssetIdentityDiagnosticCodes.CookedMissing);
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
                new AssetRecord(AssetPath.Parse("/Content/Materials/Gold.omat.json")),
                IndexedOutput(AssetPath.Parse("/Content/Materials/Gold.omat"), cookedPath),
            ]);

        _ = rows[0].PrimaryState.Should().Be(AssetState.Descriptor);
        _ = rows[0].DerivedState.Should().Be(AssetState.Stale);
    }

    [TestMethod]
    public void ReduceResolvesCookedPathsFromIndexedMetadata()
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
                new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json")),
                IndexedOutput(AssetPath.Parse("/Content/Materials/Red.omat"), cookedPath),
            ],
            project,
            new ProjectCookScope(project.ProjectId, project.ProjectRoot, workspace.SourcePath("custom-cooked")));

        _ = rows.Should().ContainSingle();
        _ = rows[0].CookedPath.Should().Be(FileSystemPath.From(cookedPath));
        _ = rows[0].DerivedState.Should().Be(AssetState.Stale);
    }

    [TestMethod]
    public void Reduce_WhenCookedOutputIsNewerThanItsDescriptor_ShouldMergeIntoOneStaleRow()
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
        var rows = new AssetIdentityReducer().Reduce(
            [
                new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json")),
                IndexedOutput(AssetPath.Parse("/Content/Materials/Red.omat"), cookedPath),
            ],
            project,
            CreateCookScope(workspace, project));

        _ = rows.Should().ContainSingle();
        _ = rows[0].PrimaryState.Should().Be(AssetState.Descriptor);
        _ = rows[0].DerivedState.Should().Be(AssetState.Stale);
    }

    [TestMethod]
    public void Reduce_WhenDescriptorJsonIsInvalid_ShouldMarkBrokenWithDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var descriptorPath = workspace.SourcePath("Content/Materials/Broken.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        File.WriteAllText(descriptorPath, "{ invalid json");

        var rows = Reduce(workspace, [new AssetRecord(AssetPath.Parse("/Content/Materials/Broken.omat.json"))]);

        _ = rows[0].PrimaryState.Should().Be(AssetState.Broken);
        _ = rows[0].DiagnosticCodes.Should().Contain(AssetIdentityDiagnosticCodes.DescriptorBroken);
    }

    [TestMethod]
    public void Missing_ShouldPreserveAuthoredIdentityAndDiagnostic()
    {
        var identity = AssetPath.Parse("/Content/Materials/Missing.omat");
        var row = AssetEntry.Missing(identity);

        _ = row.Identity.Should().Be(identity);
        _ = row.PrimaryState.Should().Be(AssetState.Missing);
        _ = row.DiagnosticCodes.Should().Contain(AssetIdentityDiagnosticCodes.ResolveMissing);
    }

    /// <summary>Missing native-reference assets retain their expected picker category.</summary>
    /// <param name="identity">The authored or cooked identity.</param>
    /// <param name="kind">The expected typed-picker category.</param>
    [TestMethod]
    [DataRow("/Content/Scripts/Orbit.oscript", AssetKind.Script)]
    [DataRow("/Content/Input/Jump.oiact", AssetKind.InputAction)]
    [DataRow("/Content/Input/Gameplay.oimap", AssetKind.InputMappingContext)]
    [DataRow("/Content/Physics/Main.opscene", AssetKind.PhysicsScene)]
    public void Missing_InfersTypedSceneReferenceKind(string identity, AssetKind kind)
    {
        var row = AssetEntry.Missing(AssetPath.Parse(identity));

        _ = row.Kind.Should().Be(kind);
    }

    private static AssetRecord IndexedOutput(AssetPath identity, string path)
    {
        var descriptor = identity.VirtualPath["/Content/".Length..];
        var root = FileSystemPath.From(path[..^(descriptor.Length + 1)]);
        return new(identity) { Cooked = new(root, descriptor, Guid.NewGuid(), new(1, 2), 1, 6, new string('0', 64)) { VirtualPath = identity } };
    }

    private static IReadOnlyList<AssetEntry> Reduce(TempWorkspace workspace, IReadOnlyList<AssetRecord> records)
    {
        var project = CreateProjectContext(workspace);
        return new AssetIdentityReducer().Reduce(
            records,
            project,
            CreateCookScope(workspace, project),
            includeMissing: true);
    }

    private static ProjectContext CreateProjectContext(TempWorkspace workspace)
        => new()
        {
            ProjectId = Guid.NewGuid(),
            Name = "Test",
            Category = Category.Games,
            ProjectRoot = workspace.Root,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
        };

    private static ProjectCookScope CreateCookScope(TempWorkspace workspace, ProjectContext project)
        => new(project.ProjectId, project.ProjectRoot, workspace.SourcePath(".cooked"));

    private static void WriteMaterial(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(
            stream,
            new MaterialSource(
                "Material",
                new MaterialPbrMetallicRoughness(
                    1.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    metallicFactor: 0.0f,
                    roughnessFactor: 0.5f),
                alphaMode: MaterialAlphaMode.Opaque,
                alphaCutoff: 0.5f,
                doubleSided: false));
    }

    private sealed partial class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = FileSystemPath.From(Path.Combine(Path.GetTempPath(), "oxygen-asset-identity-tests", Guid.NewGuid().ToString("N")));
            Directory.CreateDirectory(this.Root.FullPath);
        }

        public FileSystemPath Root { get; }

        public string SourcePath(string relative)
            => Path.Combine(this.Root.FullPath, relative.Replace('/', Path.DirectorySeparatorChar));

        public void Dispose()
        {
            if (Directory.Exists(this.Root.FullPath))
            {
                Directory.Delete(this.Root.FullPath, recursive: true);
            }
        }
    }
}
