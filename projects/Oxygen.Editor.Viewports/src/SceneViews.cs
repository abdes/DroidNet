// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Viewports;

/// <summary>
/// The session's scene views: it keeps the live editor of each open document that shows a scene, a scene document's or a
/// model document's, and forwards the requests other panels make of that scene's viewports to it.
/// </summary>
public sealed class SceneViews : ISceneViews
{
    private readonly Dictionary<DocumentId, ISceneView> editors = [];

    /// <inheritdoc/>
    public Task<bool> FrameAsync(DocumentId document, IReadOnlyList<NodeReference> nodes)
        => this.editors.TryGetValue(document, out var editor) ? editor.FrameNodesAsync(nodes) : Task.FromResult(false);

    /// <inheritdoc/>
    public Task<bool> RunCameraCommandAsync(DocumentId document, NodeId camera, SceneCameraCommand command)
        => this.editors.TryGetValue(document, out var editor) ? editor.RunCameraCommandAsync(camera, command) : Task.FromResult(false);

    /// <inheritdoc/>
    public SceneCameraViewState? CameraStateOf(DocumentId document)
        => this.editors.TryGetValue(document, out var editor) ? editor.CameraViewState() : null;

    /// <inheritdoc/>
    public Task<bool> AddInstanceAsync(DocumentId document, AssetPath source)
        => this.editors.TryGetValue(document, out var editor) ? editor.AddInstanceAsync(source) : Task.FromResult(false);

    /// <summary>Starts forwarding the requests for a document to its editor.</summary>
    /// <param name="document">The document the editor shows.</param>
    /// <param name="editor">The editor.</param>
    public void Attach(DocumentId document, ISceneView editor)
    {
        ArgumentNullException.ThrowIfNull(editor);
        this.editors[document] = editor;
    }

    /// <summary>Stops forwarding requests to an editor, when it is still the one of its document.</summary>
    /// <param name="document">The document the editor shows.</param>
    /// <param name="editor">The editor that goes away.</param>
    public void Detach(DocumentId document, ISceneView editor)
    {
        ArgumentNullException.ThrowIfNull(editor);
        if (this.editors.TryGetValue(document, out var attached) && ReferenceEquals(attached, editor))
        {
            _ = this.editors.Remove(document);
        }
    }
}
