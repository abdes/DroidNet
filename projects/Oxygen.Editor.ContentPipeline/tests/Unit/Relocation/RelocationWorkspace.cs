// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using System.Text.Json.Nodes;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>An in-memory project with one Content mount and a relocation service.</summary>
internal sealed partial class RelocationWorkspace : IDisposable
{
    private readonly Subject<AssetChange> catalogChanges = new();

    public RelocationWorkspace()
    {
        _ = this.UnsavedDocuments.Setup(documents => documents.Find(It.IsAny<IEnumerable<FileSystemPath>>())).Returns([]);
        this.Session = new(TestProject.Create(this.FileSystem).Context with
        {
            Name = "Relocation tests",
            OutputFolderViews = [new(ProjectOutputFolders.Cooked)],
        });
        this.Coordinator = new(this.Session);
        this.Files = new NativeAtomicFileStore(this.FileSystem);
        this.Catalog = Mock.Of<IAssetCatalog>(catalog => catalog.Changes == this.catalogChanges);
        this.Service = new(this.Coordinator, this.Session, this.UnsavedDocuments.Object, this.Files, this.Catalog);
    }

    public MockFileSystem FileSystem { get; } = new();

    public string Root => this.Project.ProjectRoot.FullPath;

    /// <summary>Gets the open documents with unsaved changes; it reports none until a test sets up <see cref="IUnsavedDocuments.Find"/>.</summary>
    public Mock<IUnsavedDocuments> UnsavedDocuments { get; } = new();

    public FakeContentCookCoordinator Coordinator { get; }

    public NativeAtomicFileStore Files { get; }

    public IAssetCatalog Catalog { get; }

    public AssetRelocationService Service { get; }

    public ProjectContext Project => this.Session.Context;

    /// <summary>Gets the project's session, which the session's services receive.</summary>
    public TestSession Session { get; }

    public AssetRelocationService CreateService(AssetRedirects? redirects) => new(this.Coordinator, this.Session, this.UnsavedDocuments.Object, this.Files, this.Catalog, redirects: redirects);

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

    /// <summary>Writes a scene as the editor saves it, through the production serializer.</summary>
    /// <param name="relative">The scene file, relative to the project.</param>
    /// <param name="scene">The scene.</param>
    /// <param name="token">The cancellation token.</param>
    /// <returns>The scene file's full path.</returns>
    public async Task<string> WriteSceneAsync(string relative, Scene scene, CancellationToken token)
    {
        var stream = new MemoryStream();
        await using (stream.ConfigureAwait(false))
        {
            await SceneSerializer.SerializeAsync(stream, scene).ConfigureAwait(false);
            var path = this.PathOf(relative);
            _ = this.FileSystem.Directory.CreateDirectory(this.FileSystem.Path.GetDirectoryName(path)!);
            await this.FileSystem.File.WriteAllBytesAsync(path, stream.ToArray(), token).ConfigureAwait(false);
            return path;
        }
    }

    /// <summary>Reads a scene file back through the production serializer.</summary>
    /// <param name="relative">The scene file, relative to the project.</param>
    /// <param name="token">The cancellation token.</param>
    /// <returns>The scene as it is now.</returns>
    public async Task<Scene> ReadSceneAsync(string relative, CancellationToken token)
    {
        var bytes = await this.FileSystem.File.ReadAllBytesAsync(this.PathOf(relative), token).ConfigureAwait(false);
        var stream = new MemoryStream(bytes);
        await using (stream.ConfigureAwait(false))
        {
            return await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);
        }
    }

    public string Read(string relative) => this.FileSystem.File.ReadAllText(this.PathOf(relative));

    public JsonNode ReadJson(string relative) => JsonNode.Parse(this.Read(relative))!;

    public bool Exists(string relative) => this.FileSystem.File.Exists(this.PathOf(relative)) || this.FileSystem.Directory.Exists(this.PathOf(relative));

    /// <summary>
    /// Mounts a folder outside the project as a source mount: a placeholder in <c>Project.oxy</c>, bound by the
    /// project's own <c>.mounts.json</c>.
    /// </summary>
    /// <param name="name">The mount name.</param>
    /// <param name="folder">The absolute folder.</param>
    public void MountOutside(string name, string folder)
    {
        _ = this.FileSystem.Directory.CreateDirectory(folder);
        this.FileSystem.File.WriteAllText(
            this.PathOf(ProjectMounts.FileName),
            new JsonObject { ["version"] = 1, ["mounts"] = new JsonObject { [name] = folder.Replace('\\', '/') } }.ToJsonString());
        var info = TestProject.InfoOf(this.Project);
        info.Mounts.Add(new ProjectMount(name));
        this.Session.ChangeContext(TestProject.Open(info, this.FileSystem).Context with
        {
            Name = this.Project.Name,
            OutputFolderViews = this.Project.OutputFolderViews,
        });
    }

    /// <summary>Writes a file in a folder outside the project.</summary>
    /// <param name="path">The absolute path.</param>
    /// <param name="content">The file's text.</param>
    public void WriteOutside(string path, string content)
    {
        _ = this.FileSystem.Directory.CreateDirectory(this.FileSystem.Path.GetDirectoryName(path)!);
        this.FileSystem.File.WriteAllText(path, content);
    }

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

    /// <summary>Writes an imported model: its files, and its import record, which fingerprints the files it read.</summary>
    /// <param name="bundle">The folder that holds the model, relative to the project.</param>
    /// <param name="primary">The primary source file inside the folder.</param>
    /// <returns>The model's import record.</returns>
    public ModelImportSettings WriteModel(string bundle, string primary)
    {
        _ = this.Write(bundle + "/" + primary, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"mesh.bin\"}]}");
        _ = this.Write(bundle + "/mesh.bin", "bin");
        var record = ModelRecords.Create(
            this.Project,
            "/" + bundle + "/" + primary,
            "/" + bundle + "/" + primary,
            ModelImportSettings.DefaultSettings(Path.GetFileNameWithoutExtension(primary))) with
        {
            SourceFingerprint = new(2, new string('a', 64), [new("mesh.bin", 3, new string('b', 64))]),
        };
        _ = ModelRecords.Write(this.Project, record);
        return record;
    }

    /// <summary>Gets the path of a model's import record as it is now: it moves with the model's virtual path.</summary>
    /// <param name="record">The model's record as written.</param>
    /// <returns>The record's relative path in the project.</returns>
    public string RecordOf(ModelImportSettings record)
        => this.FileSystem.Path.GetRelativePath(this.Root, this.Find(record).RecordPath);

    /// <summary>Reads a model's import record from its place in the project, found by its source identity.</summary>
    /// <param name="record">The model's record as written.</param>
    /// <returns>The record as it is now.</returns>
    public ModelImportSettings ReadRecord(ModelImportSettings record) => this.Find(record).Settings;

    public void Dispose()
    {
        this.Session.End();
        this.Service.Dispose();
        this.Coordinator.Dispose();
        this.Session.Dispose();
        this.catalogChanges.Dispose();
    }

    private ModelImportRecord Find(ModelImportSettings record)
        => new ModelImportRecords(this.Project).All.Single(found => found.Settings.SourceIdentity == record.SourceIdentity);
}
