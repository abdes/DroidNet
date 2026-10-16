// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DryIoc;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Play;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// The scene toolbar's Play controls. Play needs a camera to view the scene through, so they follow the cameras the
/// scene gains and loses.
/// </summary>
public partial class SceneEditorViewModel
{
    /// <summary>Gets the Play controls of this scene's toolbar; null without the workspace's play sessions.</summary>
    public ScenePlayToolbarViewModel? Play { get; private set; }

    private void InitializePlay()
    {
        if (this.container.Resolve<PlaySessionService>(IfUnresolved.ReturnDefault) is not { } session)
        {
            return;
        }

        this.Play = new ScenePlayToolbarViewModel(session, this.Document.Id, this.CreatePlayRequest);
        this.commandService.NodesAdded += this.OnSceneStructureChanged;
        this.commandService.NodesRemoved += this.OnSceneStructureChanged;
        this.commandService.ComponentAdded += this.OnSceneStructureChanged;
        this.commandService.ComponentRemoved += this.OnSceneStructureChanged;
        this.RefreshPlayScene();
    }

    private PlayRequest? CreatePlayRequest()
        => this.scene is { } current ? new PlayRequest(this.Document.Id, current, this.Document.File.AssetUri) : null;

    private void RefreshPlayScene()
        => this.Play?.UpdateScene(this.scene?.AllNodes.Any(static node => node.Components.OfType<CameraComponent>().Any()) == true);

    private void DisposePlay()
    {
        if (this.Play is not null)
        {
            this.commandService.NodesAdded -= this.OnSceneStructureChanged;
            this.commandService.NodesRemoved -= this.OnSceneStructureChanged;
            this.commandService.ComponentAdded -= this.OnSceneStructureChanged;
            this.commandService.ComponentRemoved -= this.OnSceneStructureChanged;
        }

        this.Play?.Dispose();
        this.Play = null;
    }

    // Adding or removing nodes or components can change what Play starts with.
    private void OnSceneStructureChanged(object? sender, EventArgs args) => this.RefreshPlayScene();
}
