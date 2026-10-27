// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// Makes the engine show the scene of the window's active document: activating the <c>Car.glb</c> tab shows the car's
/// preview scene, and activating the scene's tab shows the scene again.
/// </summary>
/// <remarks>
/// The engine shows one scene. A document that shows none, such as a material, leaves the shown scene in place.
/// </remarks>
public sealed class ShownDocumentFollower : IDisposable
{
    private readonly IDocumentArea area;
    private readonly DocumentScenes scenes;
    private readonly IScenePreview preview;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ShownDocumentFollower"/> class, following the active document.</summary>
    /// <param name="area">The window's document area.</param>
    /// <param name="scenes">The scene each open document shows.</param>
    /// <param name="preview">The engine preview, which shows one document's scene.</param>
    public ShownDocumentFollower(IDocumentArea area, DocumentScenes scenes, IScenePreview preview)
    {
        ArgumentNullException.ThrowIfNull(area);
        ArgumentNullException.ThrowIfNull(scenes);
        ArgumentNullException.ThrowIfNull(preview);
        this.area = area;
        this.scenes = scenes;
        this.preview = preview;
        this.area.ActiveDocumentChanged += this.OnActiveDocumentChanged;
        this.ShowActive();
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.area.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
    }

    private void OnActiveDocumentChanged(object? sender, EventArgs args) => this.ShowActive();

    private void ShowActive()
    {
        if (!this.disposed && this.area.ActiveDocument is { } active && this.scenes.Of(active) is not null)
        {
            _ = this.preview.Show(active.Id);
        }
    }
}
