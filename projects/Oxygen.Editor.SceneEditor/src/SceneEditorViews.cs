// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// The window's scene views: it keeps the live scene editor of each open scene document and forwards the requests other
/// panels make of a scene's viewports to it.
/// </summary>
public sealed class SceneEditorViews : ISceneViews
{
    private readonly Dictionary<DocumentId, SceneEditorViewModel> editors = [];

    /// <inheritdoc/>
    public Task<bool> FrameAsync(DocumentId document, IReadOnlyList<NodeId> nodes)
        => this.editors.TryGetValue(document, out var editor) ? editor.FrameNodesAsync(nodes) : Task.FromResult(false);

    /// <inheritdoc/>
    public Task<bool> RunCameraCommandAsync(DocumentId document, NodeId camera, SceneCameraCommand command)
        => this.editors.TryGetValue(document, out var editor) ? editor.RunCameraCommandAsync(camera, command) : Task.FromResult(false);

    /// <inheritdoc/>
    public SceneCameraViewState? CameraStateOf(DocumentId document)
        => this.editors.TryGetValue(document, out var editor) ? editor.CameraViewState() : null;

    /// <summary>Starts forwarding the requests for an editor's document to it.</summary>
    /// <param name="editor">The scene editor of a document.</param>
    internal void Attach(SceneEditorViewModel editor)
    {
        ArgumentNullException.ThrowIfNull(editor);
        this.editors[editor.Document.Id] = editor;
    }

    /// <summary>Stops forwarding requests to an editor, when it is still the one of its document.</summary>
    /// <param name="editor">The scene editor that goes away.</param>
    internal void Detach(SceneEditorViewModel editor)
    {
        ArgumentNullException.ThrowIfNull(editor);
        if (this.editors.TryGetValue(editor.Document.Id, out var attached) && ReferenceEquals(attached, editor))
        {
            _ = this.editors.Remove(editor.Document.Id);
        }
    }
}
