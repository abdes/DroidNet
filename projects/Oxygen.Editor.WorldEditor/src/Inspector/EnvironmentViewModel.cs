// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Reactive.Concurrency;
using DroidNet.Controls;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns scene identity, shared edit lifetime and parent-owned section composition.</summary>
public sealed partial class EnvironmentViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly Func<Scene, CancellationToken, Task<RuntimeEnvironmentState?>>? observeEnvironment;
    private Scene? scene;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="EnvironmentViewModel"/> class.</summary>
    /// <param name="commandService">The existing scene command service.</param>
    /// <param name="commandContextProvider">The current document context provider.</param>
    /// <param name="assetProvider">The shared content catalog.</param>
    /// <param name="inspectSceneNode">The existing hierarchy navigation operation.</param>
    /// <param name="observerScheduler">The injected catalog-notification scheduler.</param>
    /// <param name="observeEnvironment">Reads the runtime environment that renders a scene.</param>
    public EnvironmentViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null,
        IContentBrowserAssetProvider? assetProvider = null,
        Func<Guid, Task>? inspectSceneNode = null,
        IScheduler? observerScheduler = null,
        Func<Scene, CancellationToken, Task<RuntimeEnvironmentState?>>? observeEnvironment = null)
    {
        this.observeEnvironment = observeEnvironment;
        this.EditOwner = new(commandService, commandContextProvider, this.RefreshFromScene, this.ValidationFeedback);
        var scheduler = observerScheduler ?? ImmediateScheduler.Instance;
        var cubeTextures = CubemapPickerModel.CubeTextures(assetProvider);
        this.Backdrop = new(this.EditOwner, cubeTextures, scheduler);
        this.SkyLight = new(this.EditOwner, cubeTextures, scheduler);
        this.SkyAtmosphere = new(this.EditOwner);
        this.Fog = new(this.EditOwner);
        this.Exposure = new(this.EditOwner, assetProvider, observerScheduler ?? ImmediateScheduler.Instance);
        this.PostProcessing = new(this.EditOwner);
        this.AtmosphereLights = new(this.EditOwner, commandService, commandContextProvider, inspectSceneNode);
        this.SceneReferences = new(commandService, commandContextProvider, assetProvider, observerScheduler ?? ImmediateScheduler.Instance);
    }

    /// <summary>Occurs when diagnostic navigation requests realization and focus.</summary>
    public event EventHandler? FieldFocusRequested;

    /// <summary>Gets the one scene edit owner shared by ordinary sections.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the owned backdrop section.</summary>
    public BackdropSectionViewModel Backdrop { get; }

    /// <summary>Gets the owned sky light section.</summary>
    public SkyLightSectionViewModel SkyLight { get; }

    /// <summary>Gets the owned atmosphere section.</summary>
    public SkyAtmosphereSectionViewModel SkyAtmosphere { get; }

    /// <summary>Gets the owned fog section.</summary>
    public FogSectionViewModel Fog { get; }

    /// <summary>Gets the owned exposure section, including curve and texture policy.</summary>
    public ExposureSectionViewModel Exposure { get; }

    /// <summary>Gets the owned related post-processing effects.</summary>
    public PostProcessingSectionViewModel PostProcessing { get; }

    /// <summary>Gets the owned source role and observation policy.</summary>
    public AtmosphereLightsSectionViewModel AtmosphereLights { get; }

    /// <summary>Gets the owned typed scene-reference authoring section.</summary>
    public SceneReferencesSectionViewModel SceneReferences { get; }

    /// <summary>Gets the current scene identity.</summary>
    public string SceneName => this.scene?.Name ?? string.Empty;

    /// <inheritdoc />
    public Guid EditScopeId => this.EditOwner.EditScopeId;

    /// <inheritdoc />
    public override string Header => "Environment";

    /// <inheritdoc />
    public override string Description => "Scene backdrop, atmosphere, sky light, sun, fog, exposure and tone mapping.";

    /// <summary>Gets an unacknowledged diagnostic focus request.</summary>
    internal string? PendingFieldFocus { get; private set; }

    /// <summary>Gets completion of scene and source-role edits.</summary>
    internal Task PendingEdits => Task.WhenAll(this.EditOwner.Pending, this.AtmosphereLights.Pending, this.SceneReferences.Pending);

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback { get; } = new();

    /// <summary>Binds scene identity before allowing any section to submit authoring input.</summary>
    /// <param name="value">The scene, or null while node selection owns the inspector.</param>
    public void SetScene(Scene? value)
    {
        if (this.disposed || ReferenceEquals(this.scene, value))
        {
            return;
        }

        if (this.scene is { } previous)
        {
            previous.PropertyChanged -= this.OnSceneChanged;
        }

        this.EditOwner.Bind(value);
        this.scene = value;
        this.AtmosphereLights.Bind(value);
        this.SceneReferences.Bind(value);
        this.Exposure.StartAssets();
        this.Backdrop.StartAssets();
        this.SkyLight.StartAssets();
        if (value is { } currentScene)
        {
            currentScene.PropertyChanged += this.OnSceneChanged;
        }

        this.OnPropertyChanged(nameof(this.SceneName));
        this.RefreshFromScene();
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        if (items.Count > 0)
        {
            this.SetScene(value: null);
        }
        else
        {
            this.RefreshFromScene();
        }
    }

    /// <inheritdoc />
    public void BeginEditSession(string field, NumberBoxEditInteractionKind interaction) => this.EditOwner.BeginEditSession(field, interaction);

    /// <inheritdoc />
    public void CompleteEditSession(NumberBoxEditSessionEventArgs args) => this.EditOwner.CompleteEditSession(args);

    /// <inheritdoc />
    public void EndEditSession(NumberBoxEditCompletionKind completion) => this.EditOwner.EndEditSession(completion);

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        if (this.scene is { } current)
        {
            current.PropertyChanged -= this.OnSceneChanged;
        }

        this.AtmosphereLights.Dispose();
        this.SceneReferences.Dispose();
        this.Exposure.Dispose();
        this.Backdrop.Dispose();
        this.SkyLight.Dispose();
        this.EditOwner.Dispose();
        this.scene = null;
    }

    /// <summary>Requests realization of a canonical field identity.</summary>
    /// <param name="property">The stable field path.</param>
    internal void RequestFieldFocus(string property)
    {
        this.PendingFieldFocus = property;
        this.FieldFocusRequested?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Acknowledges a field only after its view accepts focus.</summary>
    internal void AcknowledgeFieldFocus() => this.PendingFieldFocus = null;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled)
    {
        this.EditOwner.SetInputEnabled(enabled);
        this.AtmosphereLights.SetInputEnabled(enabled);
        this.SceneReferences.SetInputEnabled(enabled);
    }

    private void RefreshFromScene()
    {
        if (this.disposed)
        {
            return;
        }

        var value = this.scene?.Environment ?? new SceneEnvironmentData();
        this.EditOwner.Refresh(() =>
        {
            this.Backdrop.Refresh(value);
            this.SkyLight.Refresh(value.SkyLight ?? new());
            this.SkyAtmosphere.Refresh(value.SkyAtmosphere ?? new());
            this.Fog.Refresh(value.Fog ?? new());
            this.Exposure.Refresh(value.PostProcess ?? new());
            this.PostProcessing.Refresh(value.PostProcess ?? new());
        });

        // Each authored change can alter the rendered sky light, which settles a few frames later.
        this.SkyLight.WatchRuntime(this.scene is { } current && this.observeEnvironment is { } observe
            ? cancellationToken => observe(current, cancellationToken)
            : null);
    }

    private void OnSceneChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!ReferenceEquals(sender, this.scene))
        {
            return;
        }

        if (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(Scene.Environment), StringComparison.Ordinal))
        {
            this.RefreshFromScene();
        }
        else if (string.Equals(args.PropertyName, nameof(Scene.Name), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.SceneName));
        }
    }
}
