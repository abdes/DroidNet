// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The editor's open documents with only the scene kind, over files in memory: what opens, saves, reloads and closes a
/// scene document, as the document service does.
/// </summary>
public sealed partial class SceneDocuments : IDisposable
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SceneDocuments"/> class.
    /// </summary>
    public SceneDocuments()
    {
        this.Open = new OpenDocuments([new SceneDocumentKind(FakeMaterialSlotProvider.Project, this.Interaction, this.Redirects)], this.Files);
    }

    /// <summary>Gets the open documents.</summary>
    public OpenDocuments Open { get; }

    /// <summary>Gets the files the documents read and write.</summary>
    public MemoryFileStore Files { get; } = new();

    /// <summary>Gets the lock and hide state every scene document shares.</summary>
    public FakeNodeInteraction Interaction { get; } = new();

    /// <summary>Gets the asset moves and deletions every scene document sees.</summary>
    public FakeAssetRedirects Redirects { get; } = new();

    /// <summary>
    /// Gets the file a scene is stored in: its name under <c>Content</c>.
    /// </summary>
    /// <param name="name">The scene's name.</param>
    /// <returns>The file.</returns>
    public static DocumentFile FileOf(string name)
        => new(
            Path.Combine(Path.GetTempPath(), "Content", name + AssetFileTypes.SceneDescriptor),
            new Uri($"asset:/Content/{name}{AssetFileTypes.SceneDescriptor}"));

    /// <summary>
    /// Writes a scene to its file, as an outside tool would.
    /// </summary>
    /// <param name="data">The scene data.</param>
    /// <returns>The file.</returns>
    public DocumentFile Write(SceneData data)
    {
        ArgumentNullException.ThrowIfNull(data);
        var file = FileOf(data.Name);
        this.Files.Put(file.Path, JsonSerializer.SerializeToUtf8Bytes(data as SceneFileData ?? new SceneFileData(data, explorerLayout: null), SceneFileJsonContext.Default.SceneFileData));
        return file;
    }

    /// <summary>
    /// Writes a scene to its file and opens it.
    /// </summary>
    /// <param name="data">The scene data.</param>
    /// <returns>The open document.</returns>
    public Task<Document> OpenAsync(SceneData data) => this.OpenAsync(this.Write(data));

    /// <summary>
    /// Opens a scene file.
    /// </summary>
    /// <param name="file">The file.</param>
    /// <returns>The open document.</returns>
    /// <exception cref="InvalidOperationException">The file did not open.</exception>
    public async Task<Document> OpenAsync(DocumentFile file)
    {
        var opened = await this.Open.OpenAsync(file).ConfigureAwait(true);
        return opened.Document ?? throw new InvalidOperationException($"The test scene did not open: {opened.Problem}");
    }

    /// <inheritdoc/>
    public void Dispose() => this.Open.Dispose();
}
