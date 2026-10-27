// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector;

/// <summary>The window's scene documents as the inspector follows them: the active one, its reloads and its closing.</summary>
/// <remarks>
/// A scene document that becomes active, or reloads while active, is shown; the watched one that closes is reported, so
/// the inspector stops showing its scene.
/// </remarks>
internal sealed partial class InspectedSceneDocuments : IDisposable
{
    private readonly IDocumentArea area;
    private readonly Action<Document> show;
    private readonly Action<DocumentId> closed;
    private Document? watched;

    /// <summary>Initializes a new instance of the <see cref="InspectedSceneDocuments"/> class.</summary>
    /// <param name="area">The window's document area.</param>
    /// <param name="show">Shows a scene document that became active or reloaded while active.</param>
    /// <param name="closed">Stops showing the watched scene document once it closed.</param>
    internal InspectedSceneDocuments(IDocumentArea area, Action<Document> show, Action<DocumentId> closed)
    {
        this.area = area;
        this.show = show;
        this.closed = closed;
        this.area.ActiveDocumentChanged += this.OnActiveDocumentChanged;
        this.Watch(this.area.ActiveDocument);
    }

    /// <summary>Gets the window's active document, of any kind.</summary>
    internal Document? ActiveDocument => this.area.ActiveDocument;

    /// <summary>Gets the active document when it is a scene document.</summary>
    internal DocumentId? ActiveSceneId => this.area.ActiveDocument is { Content: SceneContent } active ? active.Id : null;

    /// <inheritdoc />
    public void Dispose()
    {
        this.area.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
        this.Watch(document: null);
    }

    /// <summary>Gets the scene of an open scene document.</summary>
    /// <param name="document">The document.</param>
    /// <returns>Its scene, or <see langword="null"/> when no open scene document has that identity.</returns>
    internal Scene? SceneOf(DocumentId? document)
        => document is { } id && this.area.Documents.FirstOrDefault(candidate => candidate.Id == id)?.Content is SceneContent content
            ? content.Scene
            : null;

    /// <summary>Gets the open document that edits a scene.</summary>
    /// <param name="scene">The scene.</param>
    /// <returns>The document, or <see langword="null"/> when no open document edits that scene.</returns>
    internal Document? DocumentOf(Scene? scene)
        => scene is null ? null : this.area.Documents.FirstOrDefault(document => document.Content is SceneContent content && ReferenceEquals(content.Scene, scene));

    /// <summary>Gets the authoring that edits a scene.</summary>
    /// <param name="scene">The scene.</param>
    /// <returns>The authoring of the scene's document, or <see langword="null"/> when no open document edits it.</returns>
    internal SceneAuthoring? AuthoringOf(Scene? scene) => (this.DocumentOf(scene)?.Content as SceneContent)?.Authoring;

    private void OnActiveDocumentChanged(object? sender, EventArgs args)
    {
        if (this.watched is { } watchedDocument && !this.area.Documents.Contains(watchedDocument))
        {
            this.Watch(document: null);
            this.closed(watchedDocument.Id);
        }

        if (this.area.ActiveDocument is not { Content: SceneContent } active || ReferenceEquals(active, this.watched))
        {
            return;
        }

        this.Watch(active);
        this.show(active);
    }

    private void Watch(Document? document)
    {
        if (this.watched is { } previous)
        {
            previous.ContentReplaced -= this.OnContentReplaced;
        }

        this.watched = document is { Content: SceneContent } ? document : null;
        if (this.watched is { } current)
        {
            current.ContentReplaced += this.OnContentReplaced;
        }
    }

    private void OnContentReplaced(object? sender, DocumentContentReplacedEventArgs args)
    {
        if (sender is Document document && ReferenceEquals(this.area.ActiveDocument, document))
        {
            this.show(document);
        }
    }
}
