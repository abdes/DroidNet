// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// The Starting camera check box of a camera section: it edits the scene's one starting camera reference, so
/// checking a camera takes the start from any other and unchecking returns the scene to Automatic.
/// </summary>
/// <param name="authoringProvider">The authoring of the scene being inspected.</param>
public sealed partial class StartingCameraToggle(Func<SceneAuthoring?>? authoringProvider) : ObservableObject, IDisposable
{
    private SceneNode? camera;
    private Scene? scene;
    private bool updating;
    private Task pending = Task.CompletedTask;

    /// <summary>Gets or sets a value indicating whether the camera is the scene's authored starting camera.</summary>
    [ObservableProperty]
    public partial bool IsChecked { get; set; }

    /// <summary>Gets a value indicating whether exactly one camera is selected, so the box can be edited.</summary>
    [ObservableProperty]
    public partial bool IsAvailable { get; private set; }

    /// <summary>Gets a value indicating whether the camera starts the scene because no starting camera is set.</summary>
    [ObservableProperty]
    public partial bool IsAutomatic { get; private set; }

    /// <summary>Gets the explanation shown on hover.</summary>
    public string ToolTip => this.IsAutomatic
        ? "No starting camera is set, so Play starts on this camera, the first in the hierarchy. Check to keep it when cameras move."
        : "Play starts the scene viewing through this camera. Scripts can switch cameras while it runs.";

    /// <summary>Gets completion of the edit in flight.</summary>
    internal Task Pending => this.pending;

    /// <summary>Binds the selected cameras.</summary>
    /// <param name="cameras">The selected nodes that have a camera.</param>
    public void Bind(IReadOnlyList<SceneNode> cameras)
    {
        ArgumentNullException.ThrowIfNull(cameras);
        var single = cameras.Count == 1 ? cameras[0] : null;
        if (!ReferenceEquals(this.scene, single?.Scene))
        {
            if (this.scene is { } previous)
            {
                previous.PropertyChanged -= this.OnSceneChanged;
            }

            this.scene = single?.Scene;
            if (this.scene is { } current)
            {
                current.PropertyChanged += this.OnSceneChanged;
            }
        }

        this.camera = single;
        this.Refresh();
    }

    /// <inheritdoc />
    public void Dispose() => this.Bind([]);

    partial void OnIsCheckedChanged(bool value)
    {
        if (!this.updating)
        {
            this.pending = this.ApplyAsync(value);
        }
    }

    partial void OnIsAutomaticChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.ToolTip));
    }

    private async Task ApplyAsync(bool isStartingCamera)
    {
        if (this.camera is not { } node || this.scene is not { } current
            || authoringProvider?.Invoke() is not { } authoring || !ReferenceEquals(authoring.Scene, current))
        {
            this.Refresh();
            return;
        }

        var play = current.Play with { StartingCamera = isStartingCamera ? node.Id : null };
        if (!ScenePlayData.AreEquivalent(play, current.Play))
        {
            _ = await authoring.ExecuteAsync(new SetScenePlay(play)).ConfigureAwait(true);
        }

        this.Refresh();
    }

    private void Refresh()
    {
        this.updating = true;
        try
        {
            this.IsAvailable = this.camera is not null;
            this.IsChecked = this.camera is { } node && this.scene?.Play.StartingCamera == node.Id;
            this.IsAutomatic = this.camera is { } automatic && this.scene is { Play.StartingCamera: null } current
                && ReferenceEquals(current.EffectiveStartingCamera, automatic);
        }
        finally
        {
            this.updating = false;
        }
    }

    private void OnSceneChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(Scene.Play), StringComparison.Ordinal))
        {
            this.Refresh();
        }
    }
}
