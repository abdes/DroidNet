// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.LivePreview.Tests.TestSupport;

/// <summary>
/// The workspace's open documents with the real scene kind over files held in memory: a test writes a scene it built
/// and opens it, as the editor opens a scene file, then edits, reloads and closes the document.
/// </summary>
/// <remarks>
/// The document decodes its own scene from the file, so a test takes the document's scene and nodes from
/// <see cref="SceneContent"/>, never the scene it built.
/// </remarks>
internal sealed partial class SceneDocuments : IDisposable
{
    private const string Root = @"C:\LivePreviewTests";

    /// <summary>Initializes a new instance of the <see cref="SceneDocuments"/> class.</summary>
    public SceneDocuments()
    {
        this.Project = ProjectContext.FromProjectInfo(new ProjectInfo("Live preview", Category.Games, Root));
        this.Open = new OpenDocuments([new SceneDocumentKind(this.Project)], this.Files);
    }

    /// <summary>Gets the project every scene is opened in.</summary>
    public ProjectContext Project { get; }

    /// <summary>Gets the open documents.</summary>
    public OpenDocuments Open { get; }

    /// <summary>Gets the files the documents read and write.</summary>
    public MemoryFiles Files { get; } = new();

    /// <summary>Creates an empty scene of the project, for a test to build and open.</summary>
    /// <param name="name">The scene's name, which names its file.</param>
    /// <returns>The scene.</returns>
    public Scene NewScene(string name = "Test Scene") => new() { Name = name };

    /// <summary>Writes a scene to its file and opens it.</summary>
    /// <param name="scene">The scene the file records.</param>
    /// <returns>The open document.</returns>
    /// <exception cref="InvalidOperationException">The scene did not open.</exception>
    public async Task<Document> OpenAsync(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var file = FileOf(scene.Name);
        this.Write(file, scene);
        var opened = await this.Open.OpenAsync(file).ConfigureAwait(true);
        return opened.Document ?? throw new InvalidOperationException($"The test scene did not open: {opened.Problem}");
    }

    /// <summary>Replaces a document's file with what a scene records, as another program would, and reloads it.</summary>
    /// <param name="document">The open document.</param>
    /// <param name="scene">The scene the file now records.</param>
    /// <returns>The reload task.</returns>
    public async Task ReloadAsync(Document document, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(document);
        this.Write(document.File, scene);
        _ = await document.ReloadAsync().ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public void Dispose() => this.Open.Dispose();

    private static DocumentFile FileOf(string name)
        => new(Path.Combine(Root, "Content", name + AssetFileTypes.SceneDescriptor), new Uri($"asset:///Content/{name}{AssetFileTypes.SceneDescriptor}"));

    private void Write(DocumentFile file, Scene scene)
        => this.Files.Put(file.Path, JsonSerializer.SerializeToUtf8Bytes(scene.Dehydrate(), SceneJsonContext.Default.SceneData));

    /// <summary>Files held in memory.</summary>
    internal sealed class MemoryFiles : IAtomicFileStore
    {
        private readonly Dictionary<string, FileSnapshot> files = [with(StringComparer.OrdinalIgnoreCase)];

        /// <summary>Writes a file, as another program would.</summary>
        /// <param name="path">The file's path.</param>
        /// <param name="content">The file's content.</param>
        public void Put(string path, byte[] content)
            => this.files[path] = new FileSnapshot(ImmutableArray.Create(content), VersionOf(content));

        /// <inheritdoc/>
        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => Task.FromResult(this.files.TryGetValue(path, out var snapshot)
                ? snapshot
                : new FileSnapshot([], new FileVersion(Exists: false, string.Empty)));

        /// <inheritdoc/>
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
