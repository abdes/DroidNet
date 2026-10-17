// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DryIoc;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Play;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// The scene toolbar's Play controls. Play needs a camera to view the scene through, so they follow the cameras the
/// scene gains and loses.
/// </summary>
public partial class SceneEditorViewModel
{
    private SceneAuthoring? playAuthoring;

    /// <summary>Gets the Play controls of this scene's toolbar; null without the workspace's play sessions.</summary>
    public ScenePlayToolbarViewModel? Play { get; private set; }

    private void InitializePlay()
    {
        if (this.container.Resolve<PlaySession>(IfUnresolved.ReturnDefault) is not { } session)
        {
            return;
        }

        this.Play = new ScenePlayToolbarViewModel(session, this.Document.Id, this.SaveAllForPlayAsync, this.messenger);
        this.RefreshPlayScene();
    }

    // Play saves every document with unsaved changes through the ordinary Save and its conflict handling.
    private Task<bool> SaveAllForPlayAsync()
        => this.container.Resolve<DocumentHostViewModel>(IfUnresolved.ReturnDefault) is { } host ? host.SaveAllAsync() : Task.FromResult(true);

    // Also follows the current authoring, which a reload replaces.
    private void RefreshPlayScene()
    {
        var current = this.Play is null ? null : this.Authoring;
        if (!ReferenceEquals(current, this.playAuthoring))
        {
            this.StopObservingPlayScene();
            this.playAuthoring = current;
            if (current is not null)
            {
                current.Changed += this.OnSceneStructureChanged;
            }
        }

        this.Play?.UpdateScene(this.scene?.AllNodes.Any(static node => node.Components.OfType<CameraComponent>().Any()) == true);
    }

    private void DisposePlay()
    {
        this.StopObservingPlayScene();
        this.Play?.Dispose();
        this.Play = null;
    }

    private void StopObservingPlayScene()
    {
        if (this.playAuthoring is { } observed)
        {
            observed.Changed -= this.OnSceneStructureChanged;
        }

        this.playAuthoring = null;
    }

    // Adding or removing nodes or components can change what Play starts with.
    private void OnSceneStructureChanged(object? sender, SceneChangedEventArgs args)
    {
        if (args.Changes.Any(static change => change is SceneChange.NodesAdded or SceneChange.NodesRemoved or SceneChange.ComponentAdded or SceneChange.ComponentRemoved))
        {
            this.RefreshPlayScene();
        }
    }
}
