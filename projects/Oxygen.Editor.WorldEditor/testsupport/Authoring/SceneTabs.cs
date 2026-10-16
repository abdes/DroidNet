// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.Documents;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

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
    /// <param name="commands">The command service that owns the scene's edits in progress, or a stand-in.</param>
    /// <returns>The scene's document, with its tab.</returns>
    public static SceneTab Open(Scene scene, ISceneDocumentCommandService? commands = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var projects = new Mock<IProjectManagerService>();
        _ = projects.SetupGet(value => value.CurrentProject).Returns(scene.Project);
        var kind = new SceneDocumentKind(projects.Object, commands ?? Mock.Of<ISceneDocumentCommandService>());
        var files = new MemoryFileStore();
        var documents = new OpenDocuments([kind], files);
        var name = string.IsNullOrEmpty(scene.Name) ? "Scene" : scene.Name;
        var file = new DocumentFile(Path.Combine(Root, "Content", name + ".oscene.json"), new Uri($"asset:///Content/{name}.oscene.json"));
        files.Put(file.Path, scene);
        var opening = documents.OpenAsync(file);
        if (!opening.IsCompletedSuccessfully || opening.Result.Document is not { } document)
        {
            throw new InvalidOperationException(opening.IsCompletedSuccessfully
                ? $"The scene does not decode: {opening.Result.Problem}"
                : "An in-memory scene document opens at once.");
        }

        return new SceneTab(new DocumentTab(document, new DocumentEditorDescriptor(kind, static _ => new object()) { IsClosable = false }), documents, files);
    }

    /// <summary>Holds files in memory.</summary>
    internal sealed class MemoryFileStore : IAtomicFileStore
    {
        private readonly Dictionary<string, FileSnapshot> files = new(StringComparer.OrdinalIgnoreCase);

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

/// <summary>A scene document a test opened, with its tab and the open documents that hold it.</summary>
/// <param name="tab">The document's tab.</param>
/// <param name="documents">The open documents that hold the document.</param>
/// <param name="files">The files the document reads and writes.</param>
internal sealed class SceneTab(DocumentTab tab, OpenDocuments documents, SceneTabs.MemoryFileStore files)
{
    /// <summary>Gets the document's tab.</summary>
    public DocumentTab Tab { get; } = tab;

    /// <summary>Gets the open documents that hold the document; closing it there ends it.</summary>
    public OpenDocuments Documents { get; } = documents;

    /// <summary>Gets the scene document.</summary>
    public Document Document => this.Tab.Document;

    /// <summary>Gets the document's id.</summary>
    public DocumentId DocumentId => this.Tab.DocumentId;

    /// <summary>Gets the scene the document holds now.</summary>
    public Scene Scene => ((SceneContent)this.Document.Content).Scene;

    /// <summary>Gets the node of the document's scene that has the identity of a node the test built.</summary>
    /// <param name="node">The node the test built.</param>
    /// <returns>The document's node.</returns>
    public SceneNode NodeOf(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return this.Scene.AllNodes.Single(candidate => candidate.Id == node.Id);
    }

    /// <summary>Replaces the document's file with what a scene records, as another program would; a reload reads it.</summary>
    /// <param name="scene">The scene the file records.</param>
    public void WriteFile(Scene scene) => files.Put(this.Document.File.Path, scene);
}
