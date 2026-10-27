// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Testing;

/// <summary>A scene document a test opened, with the open documents that hold it; disposing it closes them.</summary>
public sealed partial class TestScene : IDisposable
{
    private readonly TestSceneDocuments documents;

    /// <summary>Initializes a new instance of the <see cref="TestScene"/> class: opens a scene file as its document.</summary>
    /// <param name="documents">The scene documents, which this scene owns and disposes with itself.</param>
    /// <param name="file">The scene's file.</param>
    /// <exception cref="InvalidOperationException">The scene did not open at once.</exception>
    public TestScene(TestSceneDocuments documents, DocumentFile file)
    {
        ArgumentNullException.ThrowIfNull(documents);
        this.documents = documents;
        var opening = this.documents.Open.OpenAsync(file, this.documents.Session.Lifetime);
        if (!opening.IsCompletedSuccessfully || opening.Result.Document is not { } document)
        {
            var problem = opening.IsCompletedSuccessfully ? $"The scene does not decode: {opening.Result.Problem}" : "An in-memory scene document opens at once.";
            throw new InvalidOperationException(problem);
        }

        this.Document = document;
    }

    /// <summary>Gets the open documents that hold the document; closing it there ends it.</summary>
    public OpenDocuments Documents => this.documents.Open;

    /// <summary>Gets the project that holds the scene's file.</summary>
    public ProjectContext Project => this.documents.Project.Context;

    /// <summary>Gets the scene document.</summary>
    public Document Document { get; }

    /// <summary>Gets the document's id.</summary>
    public DocumentId DocumentId => this.Document.Id;

    /// <summary>Gets the scene the document holds now.</summary>
    public Scene Scene => this.Authoring.Scene;

    /// <summary>Gets what edits the document's scene now.</summary>
    public SceneAuthoring Authoring => ((SceneContent)this.Document.Content).Authoring;

    /// <summary>Gets the explorer layout of the document's scene now.</summary>
    public IList<ExplorerEntryData> Layout => this.Authoring.Layout;

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
    public void WriteFile(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        this.documents.Write(this.Document.File, scene.Dehydrate());
    }

    /// <summary>Replaces the explorer layout of the document's scene in place, outside the history.</summary>
    /// <param name="entries">The layout's top-level entries.</param>
    public void ReplaceLayout(IEnumerable<ExplorerEntryData> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var layout = this.Layout;
        layout.Clear();
        foreach (var entry in entries)
        {
            layout.Add(entry);
        }
    }

    /// <inheritdoc/>
    public void Dispose() => this.documents.Dispose();
}
