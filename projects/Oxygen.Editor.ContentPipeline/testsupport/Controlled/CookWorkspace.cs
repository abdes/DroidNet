// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed partial class CookWorkspace : IDisposable
{
    public CookWorkspace(IReadOnlyList<ProjectMountPoint>? authoringMounts = null, IEngineContentPak? engineContent = null)
    {
        this.EngineContent = engineContent ?? new EngineContentPakFixture();
        this.Root = Path.Combine(Path.GetTempPath(), "oxygen-content-pipeline-service-tests", Guid.NewGuid().ToString("N"));
        var disk = new Testably.Abstractions.RealFileSystem();
        this.ProjectContext = (authoringMounts is null ? TestProject.Create(disk, this.Root) : TestProject.Create(disk, this.Root, authoringMounts)).Context;
        Directory.CreateDirectory(Path.Combine(this.Root, "Content", "Scenes"));
        Directory.CreateDirectory(Path.Combine(this.Root, "Content", "Materials"));
        this.Activate(this.ProjectContext);
        this.CookCoordinator = new ContentCookCoordinator(this.ContextService, NullLogger<ContentCookCoordinator>.Instance);
        this.Publication = new(this.CookCoordinator, this.ContextService, this.Files, this.Manager, this.EngineContent);
        this.Scene = new Scene { Name = "Main" };
        var producer = Path.Combine(this.Root, "producer.bin");
        File.WriteAllText(producer, "fixed test producer");
        this.Compatibility = new([new("test/producer", producer)]);
    }

    public string Root { get; }

    public DroidNet.Storage.Native.NativeAtomicFileStore Files { get; } = new(new Testably.Abstractions.RealFileSystem());

    public ProjectManagerService Manager { get; } = new(new DroidNet.Storage.Native.NativeStorageProvider(new Testably.Abstractions.RealFileSystem()));

    public CookPublicationService Publication { get; }

    /// <summary>Gets the engine content pak beneath the workspace's libraries: a declared fixture, or the installed pak for native cooks.</summary>
    public IEngineContentPak EngineContent { get; }

    public ProjectContext ProjectContext { get; }

    public ProjectContextService ContextService { get; } = new();

    public ContentCookCoordinator CookCoordinator { get; }

    public Scene Scene { get; }

    public Oxygen.Testing.TemporaryNativeArtifacts Compatibility { get; }

    public Snapshots.CookDocumentRegistry Documents { get; } = new();

    public async Task SeedEmptyPublicationAsync(string mount, CancellationToken token)
    {
        var key = Guid.CreateVersion7();
        var root = CookPublicationPaths.Generation(this.Root, key);
        Directory.CreateDirectory(root);
        Oxygen.Testing.NativeInventoryFixture.WriteIndex(root, [], key);
        await File.WriteAllBytesAsync(Path.Combine(root, CookedGeneration.MarkerFileName), [], token).ConfigureAwait(false);
        var digest = Oxygen.Testing.NativeInventoryFixture.Read(root).IndexSha256;
        var document = new CookPublicationDocument(
            CookPublicationDocument.CurrentVersion,
            this.ProjectContext.ProjectId,
            Guid.NewGuid(),
            DateTimeOffset.UtcNow,
            CookPublicationDocument.ConfigurationIdentity(this.ProjectContext),
            [new(CookPublicationRootOwner.Project, mount, key, digest, LibraryPath: null)],
            [],
            CookInputs: null);
        var path = CookPublicationPaths.Document(this.Root, document.OperationId);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var gate = await CookOutputLease.AcquireWriteAsync(this.ProjectContext.ProjectRoot, token).ConfigureAwait(false);
        var version = await this.Files.WriteAsync(path, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), DroidNet.Storage.FileVersion.Missing, token).ConfigureAwait(false);
        var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
        _ = await this.Files.WriteAsync(CookPublicationPaths.Head(this.Root), System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), DroidNet.Storage.FileVersion.Missing, token).ConfigureAwait(false);
    }

    public void Activate(ProjectContext context)
    {
        _ = TestProject.SaveProjectFile(context);
        this.ContextService.Activate(context);
    }

    public string CookedRoot(string mount)
    {
        var head = System.Text.Json.JsonSerializer.Deserialize<CookPublicationHead>(File.ReadAllBytes(CookPublicationPaths.Head(this.Root)), CookPublicationDocument.JsonOptions)!;
        var document = System.Text.Json.JsonSerializer.Deserialize<CookPublicationDocument>(File.ReadAllBytes(CookPublicationPaths.Document(this.Root, head.PublicationId)), CookPublicationDocument.JsonOptions)!;
        return document.Roots.Single(root => root.Owner == CookPublicationRootOwner.Project && string.Equals(root.Name, mount, StringComparison.Ordinal)).ResolvePath(this.Root);
    }

    public string CookedPath(string virtualPath)
    {
        var path = virtualPath.TrimStart('/');
        var separator = path.IndexOf('/', StringComparison.Ordinal);
        return Path.Combine(this.CookedRoot(path[..separator]), path[(separator + 1)..]);
    }

    public async Task<string> ExportRootAsync(string mount, CancellationToken token)
    {
        using var selected = await this.Publication.AcquireReadAsync(this.ProjectContext, token).ConfigureAwait(false);
        var source = selected.FindProjectRoot(mount)?.FullPath ?? throw new InvalidOperationException("Cook the fixture before exporting it.");
        var destination = Path.Combine(this.Root, "exports", mount);
        if (Directory.Exists(destination))
        {
            Directory.Delete(destination, recursive: true);
        }

        Directory.CreateDirectory(destination);
        foreach (var path in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories)
            .Where(static path => !string.Equals(Path.GetFileName(path), CookedGeneration.MarkerFileName, StringComparison.Ordinal)))
        {
            token.ThrowIfCancellationRequested();
            var target = Path.Combine(destination, Path.GetRelativePath(source, path));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(path, target);
        }

        return destination;
    }

    public void WriteText(string relativePath, string content)
    {
        var path = Path.Combine(this.Root, relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
    }

    public void WriteMaterial(string relativePath, string name)
    {
        var source = $$"""
                {
                  "name": "{{name}}",
                  "alpha_mode": "opaque",
                  "parameters": {
                    "base_color": [
                      1,
                      0,
                      0,
                      1
                    ],
                    "metalness": 0,
                    "roughness": 0.5,
                    "double_sided": false
                  }
                }
                """;
        this.WriteText(relativePath, source);
    }

    public string ReadText(string relativePath)
    {
        var path = Path.Combine(this.Root, relativePath.Replace('/', Path.DirectorySeparatorChar));
        return File.ReadAllText(path);
    }

    public async Task WriteSceneAsync(string relativePath)
    {
        var path = Path.Combine(this.Root, relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var stream = File.Create(path);
        await using var lifetime = stream.ConfigureAwait(false);
        await SceneSerializer.SerializeAsync(stream, this.Scene).ConfigureAwait(false);
    }

    public void Dispose()
    {
        this.ContextService.Close();
        this.CookCoordinator.Dispose();
        this.Compatibility.Dispose();
        if (Directory.Exists(this.Root))
        {
            Directory.Delete(this.Root, recursive: true);
        }
    }
}
