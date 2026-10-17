// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.Documents;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>Opens a scene a test built in memory as a scene document, and gives the document, its tab and its scene.</summary>
/// <remarks>
/// The scene is written to a file held in memory and opened through the scene kind, as the editor opens it. The document
/// therefore edits a scene of its own: a test takes the document's scene and nodes from the <see cref="SceneTab"/>.
/// </remarks>
internal static class SceneTabs
{
    private const string Root = @"C:\SceneTabs";

    /// <summary>Gets the project whose Content mount holds the scene files.</summary>
    public static ProjectContext Project { get; } = new()
    {
        ProjectId = Guid.NewGuid(),
        Name = "Scene tabs",
        Category = Category.Games,
        ProjectRoot = Root,
        AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
        LocalFolderMounts = [],
    };

    /// <summary>Opens a scene as a document of its own.</summary>
    /// <param name="scene">The scene the document's file records.</param>
    /// <param name="interaction">The lock and hide state the document's edits respect, if any.</param>
    /// <param name="redirects">The asset moves and deletions the document's edits see, if any.</param>
    /// <returns>The scene's document, with its tab; the caller disposes it.</returns>
    public static SceneTab Open(Scene scene, ISceneNodeInteraction? interaction = null, IAssetRedirects? redirects = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var projects = new Mock<IProjectManagerService>();
        _ = projects.SetupGet(value => value.CurrentProject).Returns(scene.Project);
        var kind = new SceneDocumentKind(projects.Object, interaction, redirects);
        var files = new MemoryFileStore();
        var name = string.IsNullOrEmpty(scene.Name) ? "Scene" : scene.Name;
        var file = new DocumentFile(Path.Combine(Root, "Content", name + ".oscene.json"), new Uri($"asset:///Content/{name}.oscene.json"));
        files.Put(file.Path, scene);
        return new SceneTab(kind, files, file);
    }

    /// <summary>Holds files in memory.</summary>
    internal sealed class MemoryFileStore : IAtomicFileStore
    {
        private readonly Dictionary<string, FileSnapshot> files = [with(StringComparer.OrdinalIgnoreCase)];

        /// <summary>Writes what a scene records to a file, as another program would.</summary>
        /// <param name="path">The file's path.</param>
        /// <param name="scene">The scene.</param>
        public void Put(string path, Scene scene)
        {
            var content = JsonSerializer.SerializeToUtf8Bytes(scene.Dehydrate(), SceneJsonContext.Default.SceneData);
            this.files[path] = new FileSnapshot(ImmutableArray.Create(content), VersionOf(content));
        }

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => Task.FromResult(this.files.TryGetValue(path, out var snapshot)
                ? snapshot
                : new FileSnapshot([], new FileVersion(Exists: false, string.Empty)));

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
        {
            var version = VersionOf(content.Span);
            this.files[path] = new FileSnapshot(ImmutableArray.Create(content.Span), version);
            return Task.FromResult(version);
        }

        private static FileVersion VersionOf(ReadOnlySpan<byte> content)
            => new(Exists: true, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(content)));
    }
}
