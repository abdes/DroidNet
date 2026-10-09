// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json.Nodes;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>A temporary project with one Content mount and a relocation service.</summary>
internal sealed class RelocationWorkspace : IDisposable
{
    private readonly ProjectContextService context = new();

    public RelocationWorkspace()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.Root, "Content"));
        this.context.Activate(new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Relocation tests",
            Category = Category.Games,
            ProjectRoot = this.Root,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content"), new ProjectMountPoint("Cooked", ".cooked")],
            LocalFolderMounts = [],
            Scenes = [],
        });
        this.Coordinator = new(this.context, NullLogger<ContentCookCoordinator>.Instance);
        this.Files = new NativeAtomicFileStore(new RealFileSystem());
        this.Service = new(this.Coordinator, this.context, this.Documents, this.Files);
    }

    public string Root { get; } = Path.Combine(Path.GetTempPath(), "OxygenRelocationTests", Guid.NewGuid().ToString("N"));

    public CookDocumentRegistry Documents { get; } = new();

    public ContentCookCoordinator Coordinator { get; }

    public NativeAtomicFileStore Files { get; }

    public AssetRelocationService Service { get; }

    public ProjectContext Project => this.context.ActiveProject!;

    public ProjectContextService Context => this.context;

    public AssetRelocationService CreateService(AssetRedirects? redirects) => new(this.Coordinator, this.context, this.Documents, this.Files, redirects: redirects);

    public string PathOf(string relative) => Path.GetFullPath(Path.Combine(this.Root, relative));

    public string Write(string relative, string content)
    {
        var path = this.PathOf(relative);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
        return path;
    }

    public string Read(string relative) => File.ReadAllText(this.PathOf(relative));

    public JsonNode ReadJson(string relative) => JsonNode.Parse(this.Read(relative))!;

    public bool Exists(string relative) => File.Exists(this.PathOf(relative)) || Directory.Exists(this.PathOf(relative));

    /// <summary>Writes a retained model bundle with its import sidecar.</summary>
    /// <param name="bundle">The bundle folder, relative to the project.</param>
    /// <param name="primary">The primary source file inside the bundle.</param>
    /// <param name="group">The output group the sidecar records.</param>
    /// <returns>The import settings written to the sidecar.</returns>
    public NativeSceneImportSettings WriteModel(string bundle, string primary, string group)
    {
        _ = this.Write(bundle + "/" + primary, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"mesh.bin\"}]}");
        _ = this.Write(bundle + "/mesh.bin", "bin");
        var settings = new NativeSceneImportSettings(
            4,
            NativeSceneImportSettings.ImporterIdentity,
            "Content",
            Path.GetFileNameWithoutExtension(primary),
            bundle,
            primary,
            new string('A', 64),
            ImmutableArray.Create(primary, "mesh.bin"),
            group)
        {
            MaterialSlotProvenance = NativeMaterialSlotProvenance.Create(),
        };
        File.WriteAllBytes(this.PathOf(bundle + "/" + primary + NativeSceneImportSettings.SidecarSuffix), settings.ToBytes());
        return settings;
    }

    public NativeSceneImportSettings ReadSettings(string relative) => NativeSceneImportSettings.Parse(File.ReadAllBytes(this.PathOf(relative)));

    public void Dispose()
    {
        this.context.Close();
        this.Coordinator.Dispose();
        try
        {
            Directory.Delete(this.Root, recursive: true);
        }
        catch (IOException)
        {
            // A test that failed mid-way may leave a handle; the temporary folder is disposable.
        }
    }
}
