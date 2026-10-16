// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>Opens a scene a test built in memory as a scene document, and gives its tab.</summary>
/// <remarks>
/// The document's file exists only in memory, and the kind decodes it to the scenes the test gave, so the test keeps its
/// references to the scene's nodes.
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
    /// <param name="scene">The scene the document edits.</param>
    /// <param name="commands">The command service that owns the scene's edits in progress, or a stand-in.</param>
    /// <param name="reloads">The scenes the document's reloads read, in order; the last one is read again.</param>
    /// <returns>The tab of the scene's document.</returns>
    public static SceneDocumentMetadata Open(Scene scene, ISceneDocumentCommandService? commands = null, IReadOnlyList<Scene>? reloads = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var kind = new PreparedSceneKind([scene, .. reloads ?? []], commands ?? Mock.Of<ISceneDocumentCommandService>());
        var documents = new OpenDocuments([kind], new MemoryFileStore());
        var name = string.IsNullOrEmpty(scene.Name) ? "Scene" : scene.Name;
        var opening = documents.OpenAsync(new DocumentFile(Path.Combine(Root, "Content", name + ".oscene.json"), new Uri($"asset:///Content/{name}.oscene.json")));
        if (!opening.IsCompletedSuccessfully || opening.Result.Document is not { } document)
        {
            throw new InvalidOperationException("An in-memory scene document opens at once.");
        }

        return SceneDocumentMetadata.For(document);
    }

    /// <summary>Decodes the file to the scenes it was given, one per read.</summary>
    private sealed class PreparedSceneKind(IReadOnlyList<Scene> scenes, ISceneDocumentCommandService commands) : IDocumentKind
    {
        private int reads;

        public string Name => "Scene";

        public IReadOnlyList<string> FileSuffixes { get; } = [".oscene.json"];

        public Task<DocumentDecodeResult> DecodeAsync(DocumentFile file, ReadOnlyMemory<byte> content, CancellationToken cancellationToken)
        {
            var scene = scenes[Math.Min(this.reads++, scenes.Count - 1)];
            return Task.FromResult(DocumentDecodeResult.Decoded(new SceneContent(scene, commands)));
        }
    }

    /// <summary>Holds files in memory.</summary>
    private sealed class MemoryFileStore : IAtomicFileStore
    {
        private readonly Dictionary<string, FileSnapshot> files = new(StringComparer.OrdinalIgnoreCase);

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => Task.FromResult(this.files.TryGetValue(path, out var snapshot)
                ? snapshot
                : new FileSnapshot([], new FileVersion(Exists: true, new string('0', 64))));

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
        {
            var version = new FileVersion(Exists: true, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(content.Span)));
            this.files[path] = new FileSnapshot(ImmutableArray.Create(content.Span), version);
            return Task.FromResult(version);
        }
    }
}
