// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Reactive.Concurrency;
using DroidNet.Controls;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns scene identity, shared edit lifetime and parent-owned section composition.</summary>
public sealed class EnvironmentViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostics diagnostics = new();
    private Scene? scene;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="EnvironmentViewModel"/> class.</summary>
    /// <param name="commandService">The existing scene command service.</param>
    /// <param name="commandContextProvider">The current document context provider.</param>
    /// <param name="assetProvider">The shared content catalog.</param>
    /// <param name="inspectSceneNode">The existing hierarchy navigation operation.</param>
    /// <param name="observerScheduler">The injected catalog-notification scheduler.</param>
    public EnvironmentViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null,
        IContentBrowserAssetProvider? assetProvider = null,
        Func<Guid, Task>? inspectSceneNode = null,
        IScheduler? observerScheduler = null)
    {
        this.EditOwner = new(commandService, commandContextProvider, this.RefreshFromScene, this.diagnostics);
        this.Background = new(this.EditOwner);
        this.SkyAtmosphere = new(this.EditOwner);
        this.Exposure = new(this.EditOwner, assetProvider, observerScheduler ?? ImmediateScheduler.Instance);
        this.PostProcessing = new(this.EditOwner);
        this.AtmosphereLights = new(this.EditOwner, commandService, commandContextProvider, inspectSceneNode);
    }

    /// <summary>Occurs when diagnostic navigation requests realization and focus.</summary>
    public event EventHandler? FieldFocusRequested;

    /// <summary>Gets the one scene edit owner shared by ordinary sections.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the owned background section.</summary>
    public BackgroundSectionViewModel Background { get; }

    /// <summary>Gets the owned atmosphere section.</summary>
    public SkyAtmosphereSectionViewModel SkyAtmosphere { get; }

    /// <summary>Gets the owned exposure section, including curve and texture policy.</summary>
    public ExposureSectionViewModel Exposure { get; }

    /// <summary>Gets the owned related post-processing effects.</summary>
    public PostProcessingSectionViewModel PostProcessing { get; }

    /// <summary>Gets the owned source role and observation policy.</summary>
    public AtmosphereLightsSectionViewModel AtmosphereLights { get; }

    /// <summary>Gets the current scene identity.</summary>
    public string SceneName => this.scene?.Name ?? string.Empty;

    /// <inheritdoc />
    public Guid EditScopeId => this.EditOwner.EditScopeId;

    /// <inheritdoc />
    public override string Header => "Environment";

    /// <inheritdoc />
    public override string Description => "Scene atmosphere, sun, exposure, tone mapping, and background intent.";

    /// <summary>Gets an unacknowledged diagnostic focus request.</summary>
    internal string? PendingFieldFocus { get; private set; }

    /// <summary>Gets completion of scene and source-role edits.</summary>
    internal Task PendingEdits => Task.WhenAll(this.EditOwner.Pending, this.AtmosphereLights.Pending);

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.diagnostics;

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
        this.Exposure.StartAssets();
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
        this.Exposure.Dispose();
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
            this.Background.Refresh(value.BackgroundColor);
            this.SkyAtmosphere.Refresh(value.AtmosphereEnabled, value.SkyAtmosphere ?? new());
            this.Exposure.Refresh(value.PostProcess ?? new());
            this.PostProcessing.Refresh(value.PostProcess ?? new());
        });
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
