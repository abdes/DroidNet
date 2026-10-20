// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Reactive.Subjects;
using System.Text.Json.Nodes;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>An in-memory project with one Content mount and a relocation service.</summary>
internal sealed class RelocationWorkspace : IDisposable
{
    private readonly ProjectContextService context = new();
    private readonly Subject<AssetChange> catalogChanges = new();

    public RelocationWorkspace()
    {
        this.context.Activate(TestProject.Create(this.FileSystem).Context with
        {
            Name = "Relocation tests",
            AuthoringMounts = [new ProjectMountPoint("Content", "Content"), new ProjectMountPoint("Cooked", ".cooked")],
        });
        this.Coordinator = new(this.context);
        this.Files = new NativeAtomicFileStore(this.FileSystem);
        this.Catalog = Mock.Of<IAssetCatalog>(catalog => catalog.Changes == this.catalogChanges);
        this.Service = new(this.Coordinator, this.context, this.Documents, this.Files, this.Catalog);
    }

    public MockFileSystem FileSystem { get; } = new();

    public string Root => this.Project.ProjectRoot.FullPath;

    public CookDocumentRegistry Documents { get; } = new();

    public FakeContentCookCoordinator Coordinator { get; }

    public NativeAtomicFileStore Files { get; }

    public IAssetCatalog Catalog { get; }

    public AssetRelocationService Service { get; }

    public ProjectContext Project => this.context.ActiveProject!;

    public ProjectContextService Context => this.context;

    public AssetRelocationService CreateService(AssetRedirects? redirects) => new(this.Coordinator, this.context, this.Documents, this.Files, this.Catalog, redirects: redirects);

    /// <summary>Reports a catalog change, as the file watcher does after an edit outside a relocation.</summary>
    /// <param name="identity">The changed asset's catalog identity, which is its source file's virtual path.</param>
    public void ChangeInCatalog(AssetPath identity) => this.catalogChanges.OnNext(new(AssetChangeKind.Updated, identity));

    public string PathOf(string relative) => this.FileSystem.Path.GetFullPath(this.FileSystem.Path.Combine(this.Root, relative));

    public FileSystemPath FileOf(string relative) => FileSystemPath.From(this.PathOf(relative));

    public string Write(string relative, string content)
    {
        var path = this.PathOf(relative);
        _ = this.FileSystem.Directory.CreateDirectory(this.FileSystem.Path.GetDirectoryName(path)!);
        this.FileSystem.File.WriteAllText(path, content);
        return path;
    }

    public string Read(string relative) => this.FileSystem.File.ReadAllText(this.PathOf(relative));

    public JsonNode ReadJson(string relative) => JsonNode.Parse(this.Read(relative))!;

    public bool Exists(string relative) => this.FileSystem.File.Exists(this.PathOf(relative)) || this.FileSystem.Directory.Exists(this.PathOf(relative));

    /// <summary>Creates a folder in the project.</summary>
    /// <param name="relative">The folder, relative to the project.</param>
    public void CreateFolder(string relative) => _ = this.FileSystem.Directory.CreateDirectory(this.PathOf(relative));

    /// <summary>Reads every project file outside the build folder, by its path relative to the project.</summary>
    /// <returns>Each file's text.</returns>
    public Dictionary<string, string> Snapshot()
        => this.FileSystem.Directory.EnumerateFiles(this.Root, "*", SearchOption.AllDirectories)
            .Select(path => (Path: path, Relative: this.FileSystem.Path.GetRelativePath(this.Root, path)))
            .Where(static file => !file.Relative.StartsWith(".build", StringComparison.Ordinal))
            .ToDictionary(static file => file.Relative, file => this.FileSystem.File.ReadAllText(file.Path), StringComparer.Ordinal);

    /// <summary>Writes a retained model bundle with its import sidecar.</summary>
    /// <param name="bundle">The bundle folder, relative to the project.</param>
    /// <param name="primary">The primary source file inside the bundle.</param>
    /// <returns>The import settings written to the sidecar.</returns>
    public NativeSceneImportSettings WriteModel(string bundle, string primary)
    {
        _ = this.Write(bundle + "/" + primary, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"mesh.bin\"}]}");
        _ = this.Write(bundle + "/mesh.bin", "bin");
        var settings = new NativeSceneImportSettings(
            NativeSceneImportSettings.CurrentSchemaVersion,
            NativeSceneImportSettings.ImporterIdentity,
            Path.GetFileNameWithoutExtension(primary),
            bundle,
            primary,
            new string('A', 64),
            ImmutableArray.Create(primary, "mesh.bin"))
        {
            MaterialSlotProvenance = NativeMaterialSlotProvenance.Create(),
        };
        this.FileSystem.File.WriteAllBytes(this.PathOf(bundle + "/" + primary + NativeSceneImportSettings.SidecarSuffix), settings.ToBytes());
        return settings;
    }

    public NativeSceneImportSettings ReadSettings(string relative) => NativeSceneImportSettings.Parse(this.FileSystem.File.ReadAllBytes(this.PathOf(relative)));

    public void Dispose()
    {
        this.context.Close();
        this.Service.Dispose();
        this.Coordinator.Dispose();
        this.catalogChanges.Dispose();
    }
}
