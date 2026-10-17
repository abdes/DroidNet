// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>A scene document a test opened, with its tab and the open documents that hold it; disposing it closes them.</summary>
internal sealed partial class SceneTab : IDisposable
{
    private readonly SceneTabs.MemoryFileStore files;

    /// <summary>Initializes a new instance of the <see cref="SceneTab"/> class: opens a scene file as its document.</summary>
    /// <param name="kind">The scene kind.</param>
    /// <param name="files">The files the document reads and writes.</param>
    /// <param name="file">The scene's file.</param>
    /// <exception cref="InvalidOperationException">The scene did not open at once.</exception>
    public SceneTab(SceneDocumentKind kind, SceneTabs.MemoryFileStore files, DocumentFile file)
    {
        this.files = files;
        this.Documents = new OpenDocuments([kind], files);
        var opening = this.Documents.OpenAsync(file);
        if (!opening.IsCompletedSuccessfully || opening.Result.Document is not { } document)
        {
            var problem = opening.IsCompletedSuccessfully ? $"The scene does not decode: {opening.Result.Problem}" : "An in-memory scene document opens at once.";
            this.Documents.Dispose();
            throw new InvalidOperationException(problem);
        }

        this.Tab = new DocumentTab(document, new DocumentEditorDescriptor(kind, static _ => new object()) { IsClosable = false });
    }

    /// <summary>Gets the document's tab.</summary>
    public DocumentTab Tab { get; }

    /// <summary>Gets the open documents that hold the document; closing it there ends it.</summary>
    public OpenDocuments Documents { get; }

    /// <summary>Gets the scene document.</summary>
    public Document Document => this.Tab.Document;

    /// <summary>Gets the document's id.</summary>
    public DocumentId DocumentId => this.Tab.DocumentId;

    /// <summary>Gets the scene the document holds now.</summary>
    public Scene Scene => this.Authoring.Scene;

    /// <summary>Gets what edits the document's scene now.</summary>
    public SceneAuthoring Authoring => ((SceneContent)this.Document.Content).Authoring;

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
    public void WriteFile(Scene scene) => this.files.Put(this.Document.File.Path, scene);

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Tab.Dispose();
        this.Documents.Dispose();
    }
}
