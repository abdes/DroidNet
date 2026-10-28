// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.ModelEditor.Settings;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Viewports;
using Oxygen.Editor.Viewports.Settings;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
/// The Model Editor of a model document: the model's preview scene in one viewport, read-only, beside its import
/// settings, with the toolbar that saves, reimports, places the model in the open scene and browses to its assets
/// (model editor 4 to 6).
/// </summary>
/// <remarks>
/// The docked Scene Explorer and Properties pane show the model's nodes while its tab is active, and the engine shows its
/// preview scene; nothing here moves a pane.
/// </remarks>
public sealed partial class ModelEditorViewModel : ObservableObject, IDocumentWindowEditor, ISceneView, IDisposable
{
    private const string ViewportSelectionSource = "Viewport";

    private readonly ModelEditorServices services;
    private readonly ILogger logger;
    private readonly SynchronizationContext? owner;
    private IDisposable? assetSubscription;
    private DocumentWindowServices? window;
    private SceneAuthoring? observedPreview;

    // Set while the viewport's controls take the preview's environment, which they then must not write back.
    private bool showingEnvironment;
    private bool isCreatingViewport;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ModelEditorViewModel"/> class.</summary>
    /// <param name="tab">The model document's tab.</param>
    /// <param name="services">The session's services that the model editors use.</param>
    public ModelEditorViewModel(DocumentTab tab, ModelEditorServices services)
    {
        ArgumentNullException.ThrowIfNull(tab);
        ArgumentNullException.ThrowIfNull(services);
        this.services = services;
        this.logger = services.LoggerFactory.CreateLogger<ModelEditorViewModel>();
        this.owner = SynchronizationContext.Current;
        this.Tab = tab;
        this.Document = tab.Document;
        this.PreviewSettingsPanel = services.Viewports.CreatePreviewSettings();
        this.Settings = new ModelImportSettingsPanelViewModel();
        this.Settings.SettingEdited += this.OnSettingEdited;
        this.Model.Changed += this.OnModelChanged;
        services.Problems.ProblemsChanged += this.OnProblemsChanged;
        services.Imports.ImportingChanged += this.OnImportingChanged;
        services.CookRuns.CookCompleted += this.OnCookCompletedAsync;
        _ = services.Layout.EnsureLoadedAsync();
        this.ShowRecord();
        this.UpdateProblems();

        services.Views.Attach(this.Document.Id, this);
        services.ScenePreview.ReadyChanged += this.OnPreviewReadyChanged;
        services.Selection.SelectionChanged += this.OnSelectionChanged;
        services.OpenDocuments.Opened += this.OnOpenDocumentsChanged;
        services.OpenDocuments.Closed += this.OnOpenDocumentsChanged;
        this.Document.StateChanged += this.OnDocumentStateChanged;
        this.assetSubscription = services.Assets.Items.Subscribe(this.OnAssetsChanged);
        if (services.ScenePreview.IsReady(this.Document.Id))
        {
            // The engine showed the model before this editor existed.
            this.OnSceneReady();
        }
    }

    /// <summary>Gets the model document's tab.</summary>
    public DocumentTab Tab { get; }

    /// <summary>Gets the model document.</summary>
    public Document Document { get; }

    /// <summary>Gets the model's source file, which the document shows and a scene places.</summary>
    public AssetPath Source => ((ModelContent)this.Document.Content).Source;

    /// <summary>Gets the viewport that shows the model; none until the engine shows the model's preview scene.</summary>
    [ObservableProperty]
    public partial ViewportViewModel? Viewport { get; private set; }

    /// <summary>Gets the preview settings the toolbar's Settings flyout edits, shared with the scene editor.</summary>
    public PreviewSettingsViewModel PreviewSettingsPanel { get; }

    /// <summary>Gets where the model's import stands.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ImportStatusText), nameof(HasImportStatus), nameof(ImportStatusSeverity))]
    [NotifyCanExecuteChangedFor(nameof(ReimportCommand))]
    public partial ModelImportState ImportState { get; private set; }

    /// <summary>Gets what the toolbar's status says of the import, such as "Source changed" or "Import failed".</summary>
    /// <remarks>An open model whose last import is current says nothing: only a state that asks for attention shows.</remarks>
    public string ImportStatusText => this.ImportState == ModelImportState.Imported ? string.Empty : ModelImportStatus.TextOf(this.ImportState);

    /// <summary>Gets a value indicating whether the toolbar shows the import status.</summary>
    public bool HasImportStatus => this.ImportStatusText.Length != 0;

    /// <summary>Gets how serious the import status is: a warning or an error is colored as one.</summary>
    public DiagnosticSeverity ImportStatusSeverity => ModelImportStatus.SeverityOf(this.ImportState);

    /// <summary>Gets what <em>Add to scene</em> does, or why it is unavailable.</summary>
    public string AddToSceneToolTip => this.FindOpenScene() is { } scene
        ? $"Place {this.Document.Title} in {scene.Title}, in front of its viewport, and select it."
        : "Open a scene to place the model in.";

    /// <inheritdoc/>
    public void AttachWindow(DocumentWindowServices window)
    {
        ArgumentNullException.ThrowIfNull(window);
        this.window = window;
    }

    /// <inheritdoc/>
    public void DetachWindow() => this.window = null;

    /// <inheritdoc/>
    public async Task<bool> FrameNodesAsync(IReadOnlyList<NodeReference> nodes)
    {
        if (this.disposed || this.Viewport is not { } viewport)
        {
            return false;
        }

        await viewport.FrameAsync(nodes).ConfigureAwait(true);
        return true;
    }

    /// <inheritdoc/>
    /// <remarks>A model is looked at with the editor camera: its cameras are not looked through.</remarks>
    public Task<bool> RunCameraCommandAsync(NodeId nodeId, SceneCameraCommand command) => Task.FromResult(false);

    /// <inheritdoc/>
    public SceneCameraViewState? CameraViewState() => null;

    /// <inheritdoc/>
    /// <remarks>A model takes no instance: it is placed in a scene with <see cref="AddToSceneCommand"/>.</remarks>
    public Task<bool> AddInstanceAsync(AssetPath source) => Task.FromResult(false);

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.services.Views.Detach(this.Document.Id, this);
        this.services.ScenePreview.ReadyChanged -= this.OnPreviewReadyChanged;
        this.services.Selection.SelectionChanged -= this.OnSelectionChanged;
        this.services.OpenDocuments.Opened -= this.OnOpenDocumentsChanged;
        this.services.OpenDocuments.Closed -= this.OnOpenDocumentsChanged;
        this.Document.StateChanged -= this.OnDocumentStateChanged;
        this.Settings.SettingEdited -= this.OnSettingEdited;
        this.Model.Changed -= this.OnModelChanged;
        this.services.Problems.ProblemsChanged -= this.OnProblemsChanged;
        this.services.Imports.ImportingChanged -= this.OnImportingChanged;
        this.services.CookRuns.CookCompleted -= this.OnCookCompletedAsync;
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
        this.ObservePreview(authoring: null);
        this.SaveViewportState();
        this.Viewport?.PropertyChanged -= this.OnViewportPropertyChanged;
        this.Viewport?.StateChanged -= this.OnViewportStateChanged;
        this.Viewport?.Dispose();
        this.Viewport = null;
    }

    // The scene a model is placed in: the open scene document, there is one per workspace.
    private Document? FindOpenScene() => this.services.OpenDocuments.Documents.FirstOrDefault(static document => document.Content is SceneContent);

    private bool CanSave() => !this.disposed && this.Document.IsDirty;

    /// <summary>Saves the model document (Ctrl+S).</summary>
    [RelayCommand(CanExecute = nameof(CanSave))]
    private async Task SaveAsync() => _ = await this.services.Documents.SaveAsync(this.Document).ConfigureAwait(true);

    private bool CanReimport() => !this.disposed && !this.services.Imports.IsImporting(this.Source) && ModelImportStatus.CanReimport(this.ImportState);

    /// <summary>Imports the model again now, with the settings the panel shows.</summary>
    /// <remarks>
    /// Unsaved setting edits are saved first, which imports with them; otherwise the saved settings import again even
    /// when the last import is current. Either way the import runs: a reimport is never answered by a check.
    /// </remarks>
    /// <returns>A task that completes when the import did, or failed.</returns>
    [RelayCommand(CanExecute = nameof(CanReimport))]
    private async Task ReimportAsync()
    {
        if (this.Document.IsDirty)
        {
            await this.SaveAsync().ConfigureAwait(true);
            return;
        }

        await this.services.Imports.ReimportAsync(this.Source).ConfigureAwait(true);
    }

    /// <summary>Returns the model's preview to the default sky, sun and exposure, and forgets its file.</summary>
    [RelayCommand]
    private async Task ResetPreviewAsync() => _ = await this.services.Scenes.ResetPreviewAsync(this.Document).ConfigureAwait(true);

    private bool CanAddToScene() => !this.disposed && this.FindOpenScene() is not null;

    /// <summary>
    /// Places the model in the open scene, in front of its viewport: the scene's tab is activated and the new instance root
    /// selected (scene instances 8.6).
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanAddToScene))]
    private async Task AddToSceneAsync()
    {
        if (this.FindOpenScene() is not { } scene)
        {
            return;
        }

        var source = this.Source;
        this.LogAddToSceneRequested(source);

        // The scene is shown first: the instance goes in front of its viewport, where the user sees it placed.
        if (this.window?.Area is { } area)
        {
            _ = await area.ShowAsync(scene).ConfigureAwait(true);
        }

        _ = await this.services.Views.AddInstanceAsync(scene.Id, source).ConfigureAwait(true);
    }

    /// <summary>Reveals the selected nodes' geometries in the Content Explorer, else the model file (Ctrl+B).</summary>
    [RelayCommand]
    private async Task BrowseToAssetAsync()
    {
        if (this.window?.Reveal is not { } reveal)
        {
            return;
        }

        var geometries = this.SelectedGeometries();
        _ = await reveal.RevealAsync(geometries.Count > 0 ? geometries : [this.Source]).ConfigureAwait(true);
    }

    // The geometries the selected nodes of the model render, as the model's structure was last read.
    private List<AssetPath> SelectedGeometries()
    {
        if (this.services.Structures.Find(this.services.Session.Context, this.Source) is not { } structure)
        {
            return [];
        }

        return [.. this.services.Selection.GetContext(this.Document.Id).SelectedNodes
            .Where(static node => !node.IsOwned)
            .Select(node => structure.Find(node.Path)?.Geometry)
            .OfType<AssetPath>()
            .Distinct()];
    }

    private IReadOnlyList<NodeReference> SelectedNodes() => this.services.Selection.GetContext(this.Document.Id).SelectedNodes;

    private void ApplyViewportSelection(ViewportSelection selection)
    {
        if (!this.disposed && selection.Combine(this.SelectedNodes()) is { } nodes)
        {
            this.services.Selection.Publish(this.Document.Id, SceneSelectionContext.ForNodes(nodes), ViewportSelectionSource);
        }
    }

    private void OutlineSelection()
    {
        var context = this.services.Selection.GetContext(this.Document.Id);
        _ = this.services.Viewports.OutlineSelectionAsync(this.Document.Id, context.SelectedNodes, context.PrimaryNode, ModelEditorResults.ScopeOf(this.Source));
    }

    // The model's nodes, which the viewport's statistics count.
    private int CountModelNodes() => this.services.Structures.Find(this.services.Session.Context, this.Source)?.Nodes.Length ?? 0;

    // The viewport is made once the engine shows the model, with the camera the user left there, else framing the model.
    private async void OnSceneReady()
    {
        if (this.Viewport is null && !this.isCreatingViewport)
        {
            this.isCreatingViewport = true;
            try
            {
                this.Viewport = await this.CreateViewportAsync().ConfigureAwait(true);
            }
            finally
            {
                this.isCreatingViewport = false;
            }
        }

        if (this.disposed || this.Viewport is null)
        {
            return;
        }

        // A reload builds the preview again: the viewport follows the one the engine shows.
        this.ObservePreview(this.services.Scenes.Of(this.Document)?.Authoring);
        this.ShowPreviewEnvironment();

        // The model takes no edit: the engine shows no gizmo of the scene shown before it.
        _ = this.services.Viewports.HideTransformGizmoAsync(this.Document.Id);
        this.OutlineSelection();
    }

    // The pane's state is kept per user under the preview scene's identity, the model's source identity; it is restored
    // before the pane has a view.
    private async Task<ViewportViewModel?> CreateViewportAsync()
    {
        var viewport = this.services.Viewports.CreateViewport(this.Document.Id);
        viewport.Profile = ViewportProfile.Viewing;
        viewport.FramesSceneOnce = true;
        viewport.IsFocused = true;
        if (this.PreviewSceneId() is { } sceneId
            && await this.services.ViewportStates.RestoreAsync(sceneId).ConfigureAwait(true) is { Panes: [var pane, ..] })
        {
            viewport.RestoreState(pane, sceneCamera: null);
            viewport.FramesSceneOnce = pane.EditorCamera is null;
        }

        if (this.disposed)
        {
            viewport.Dispose();
            return null;
        }

        viewport.SelectionPicked = this.ApplyViewportSelection;
        viewport.SelectedNodesProvider = this.SelectedNodes;
        viewport.NodeCountProvider = this.CountModelNodes;
        viewport.PropertyChanged += this.OnViewportPropertyChanged;
        viewport.StateChanged += this.OnViewportStateChanged;
        return viewport;
    }

    private SceneId? PreviewSceneId() => this.services.Scenes.Of(this.Document)?.Scene.Id;

    private void SaveViewportState()
    {
        if (this.Viewport is { } viewport && this.PreviewSceneId() is { } sceneId)
        {
            this.services.ViewportStates.Update(sceneId, new SceneViewportState(SceneViewLayout.OnePane, FocusedPane: 0, [viewport.CaptureState()]));
        }
    }

    private void OnViewportStateChanged(object? sender, EventArgs args)
    {
        if (!this.disposed)
        {
            this.SaveViewportState();
        }
    }

    private void OnPreviewReadyChanged(object? sender, ScenePreviewEventArgs args)
    {
        if (!this.disposed && args.Document == this.Document.Id && this.services.ScenePreview.IsReady(args.Document))
        {
            this.OnSceneReady();
        }
    }

    private void ObservePreview(SceneAuthoring? authoring)
    {
        if (ReferenceEquals(this.observedPreview, authoring))
        {
            return;
        }

        this.observedPreview?.Changed -= this.OnPreviewChanged;
        this.observedPreview = authoring;
        this.observedPreview?.Changed += this.OnPreviewChanged;
    }

    // The preview's environment, edited in the viewport or in Properties, shows in the viewport's controls.
    private void OnPreviewChanged(object? sender, SceneChangedEventArgs args)
    {
        if (!this.disposed)
        {
            this.ShowPreviewEnvironment();
        }
    }

    private void ShowPreviewEnvironment()
    {
        if (this.Viewport is not { } viewport)
        {
            return;
        }

        var environment = this.services.Scenes.EnvironmentOf(this.Document);
        this.showingEnvironment = true;
        try
        {
            viewport.ShowPreviewEnvironment = environment.IsShown;
            viewport.ExposureCompensation = environment.ExposureCompensationEv;
        }
        finally
        {
            this.showingEnvironment = false;
        }
    }

    // The viewport's Preview environment and exposure light the model's preview scene.
    private void OnViewportPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (this.disposed || this.showingEnvironment || sender is not ViewportViewModel viewport
            || args.PropertyName is not (nameof(ViewportViewModel.ShowPreviewEnvironment) or nameof(ViewportViewModel.ExposureCompensation)))
        {
            return;
        }

        _ = this.services.Scenes.SetPreviewEnvironmentAsync(
            this.Document,
            new PreviewEnvironment(viewport.ShowPreviewEnvironment, (float)viewport.ExposureCompensation));
    }

    private void OnSelectionChanged(object? sender, SceneSelectionChangedEventArgs args)
    {
        if (!this.disposed && args.DocumentId == this.Document.Id)
        {
            this.OutlineSelection();
            _ = this.ShowSelectedMeshAsync();
        }
    }

    private void OnOpenDocumentsChanged(object? sender, DocumentEventArgs args)
    {
        if (!this.disposed)
        {
            this.OnPropertyChanged(nameof(this.AddToSceneToolTip));
            this.AddToSceneCommand.NotifyCanExecuteChanged();
        }
    }

    private void OnDocumentStateChanged(object? sender, EventArgs args)
    {
        if (!this.disposed)
        {
            this.SaveCommand.NotifyCanExecuteChanged();
        }
    }

    // The feed reports on any thread; the state is the editor's, on its own thread.
    private void OnAssetsChanged(IReadOnlyList<AssetEntry> assets)
    {
        if (this.owner is { } context && !ReferenceEquals(SynchronizationContext.Current, context))
        {
            context.Post(_ => this.UpdateImportState(assets), state: null);
            return;
        }

        this.UpdateImportState(assets);
    }

    private void UpdateImportState(IReadOnlyList<AssetEntry> assets)
    {
        if (!this.disposed)
        {
            this.ImportState = ModelImportStatus.Of(this.Source, assets);
            this.Settings.UpdateMaterials(assets);
        }
    }
}
