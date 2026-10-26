// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Testing;

/// <summary>
/// Scene documents for a test: the editor's open documents with the real scene kind, over the files of a
/// <see cref="TestProject"/>. A test writes a scene it built to its file, as another program would, and opens it as
/// the editor opens a scene file.
/// </summary>
/// <remarks>
/// Files are read and written by the real atomic store on the project's file system, so a save, a reload and a write
/// conflict behave as they do on disk. The document decodes its own scene from the file: a test takes the document's
/// scene and nodes from its <see cref="SceneContent"/>, never the scene it built.
/// </remarks>
public sealed partial class TestSceneDocuments : IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="TestSceneDocuments"/> class.</summary>
    /// <param name="project">The project whose <see cref="TestProject.ContentMount"/> mount holds the scene files.</param>
    /// <param name="interaction">The lock and hide state every scene document's edits respect, if any.</param>
    /// <param name="redirects">The asset moves and deletions every scene document's edits see, if any.</param>
    /// <param name="otherKinds">The kinds of the other documents the test opens beside its scenes, if any.</param>
    /// <param name="references">The saved authored references every scene document's instance sources are checked against, if any.</param>
    public TestSceneDocuments(TestProject project, ISceneNodeInteraction? interaction = null, IAssetRedirects? redirects = null, IEnumerable<IDocumentKind>? otherKinds = null, IAuthoredReferences? references = null)
    {
        ArgumentNullException.ThrowIfNull(project);
        this.Project = project;
        this.Files = new GatedFileStore(new NativeAtomicFileStore(project.Context.FileSystem), this);
        this.Session = new TestSession(project.Context);
        this.Kind = new SceneDocumentKind(interaction, redirects, references);
        this.Open = new OpenDocuments([this.Kind, .. otherKinds ?? []], this.Files);
    }

    /// <summary>Gets the project the scenes belong to.</summary>
    public TestProject Project { get; }

    /// <summary>Gets the project session the scenes open in.</summary>
    public TestSession Session { get; }

    /// <summary>Gets the scene kind every document opens with.</summary>
    public SceneDocumentKind Kind { get; }

    /// <summary>Gets the open documents.</summary>
    public OpenDocuments Open { get; }

    /// <summary>Gets the store the documents read and write their files with.</summary>
    public IAtomicFileStore Files { get; }

    /// <summary>
    /// Gets or sets what a read of a file waits for, given the file's full path: a test holds a read with it, to order
    /// a read against other work.
    /// </summary>
    public Func<string, Task>? BeforeRead { get; set; }

    /// <summary>Gets the file of a scene: its name under the project's <see cref="TestProject.ContentMount"/> mount.</summary>
    /// <param name="name">The scene's name.</param>
    /// <returns>The file.</returns>
    public DocumentFile FileOf(string name)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        var folder = "/" + TestProject.ContentMount + "/" + name;
        return new DocumentFile(this.Project.PathOf(folder + AssetFileTypes.SceneDescriptor), AssetPath.Parse(folder + AssetFileTypes.Scene));
    }

    /// <summary>Writes a scene to the file of its name, as another program would.</summary>
    /// <param name="data">The scene; a <see cref="SceneFileData"/> keeps its explorer layout.</param>
    /// <param name="layout">The explorer layout the file records beside the scene; when given, it replaces the one of
    /// <paramref name="data"/>.</param>
    /// <returns>The file.</returns>
    public DocumentFile Write(SceneData data, IList<ExplorerEntryData>? layout = null)
    {
        ArgumentNullException.ThrowIfNull(data);
        var file = this.FileOf(data.Name);
        this.Write(file, data, layout);
        return file;
    }

    /// <summary>Replaces a scene file with what a scene records, as another program would; a reload reads it.</summary>
    /// <param name="file">The scene's file.</param>
    /// <param name="data">The scene; a <see cref="SceneFileData"/> keeps its explorer layout.</param>
    /// <param name="layout">The explorer layout the file records beside the scene; when given, it replaces the one of
    /// <paramref name="data"/>.</param>
    public void Write(DocumentFile file, SceneData data, IList<ExplorerEntryData>? layout = null)
    {
        ArgumentNullException.ThrowIfNull(file);
        ArgumentNullException.ThrowIfNull(data);
        var content = layout is null && data is SceneFileData stored ? stored : new SceneFileData(data, layout);
        var path = file.Path.FullPath;
        _ = this.Project.Context.FileSystem.Directory.CreateDirectory(this.Project.Context.FileSystem.Path.GetDirectoryName(path)!);
        this.Project.Context.FileSystem.File.WriteAllBytes(path, JsonSerializer.SerializeToUtf8Bytes(content, SceneFileJsonContext.Default.SceneFileData));
    }

    /// <summary>Writes a scene to the file of its name and opens it.</summary>
    /// <param name="data">The scene.</param>
    /// <param name="layout">The explorer layout the file records beside the scene, if any.</param>
    /// <returns>The open document.</returns>
    public Task<Document> OpenAsync(SceneData data, IList<ExplorerEntryData>? layout = null)
        => this.OpenAsync(this.Write(data, layout));

    /// <summary>Opens a scene file.</summary>
    /// <param name="file">The file.</param>
    /// <returns>The open document.</returns>
    /// <exception cref="InvalidOperationException">The file did not open.</exception>
    public async Task<Document> OpenAsync(DocumentFile file)
    {
        var opened = await this.Open.OpenAsync(file).ConfigureAwait(true);
        return opened.Document ?? throw new InvalidOperationException($"The test scene did not open: {opened.Problem}");
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Open.Dispose();
        this.Session.Dispose();
    }

    // The real store, whose reads first wait for what the test set.
    private sealed class GatedFileStore(IAtomicFileStore inner, TestSceneDocuments documents) : IAtomicFileStore
    {
        /// <inheritdoc/>
        public async Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
        {
            if (documents.BeforeRead is { } gate)
            {
                await gate(path).ConfigureAwait(false);
            }

            return await inner.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        }

        /// <inheritdoc/>
        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
            => inner.WriteAsync(path, content, expected, cancellationToken);
    }
}
