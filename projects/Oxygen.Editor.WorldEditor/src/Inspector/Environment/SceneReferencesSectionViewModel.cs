// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.World.Inspector.Presentation;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns typed scene asset-reference assignments and retained unresolved references.</summary>
public sealed partial class SceneReferencesSectionViewModel : ObservableObject, IDisposable
{
    private readonly ISceneDocumentCommandService? commandService;
    private readonly Func<SceneDocumentCommandContext?>? commandContextProvider;
    private readonly IContentBrowserAssetProvider? assetProvider;
    private readonly IScheduler observerScheduler;
    private readonly List<ContentBrowserAssetItem> assets = [];
    private IDisposable? assetSubscription;
    private Scene? scene;
    private Task pending = Task.CompletedTask;
    private bool isBusy;
    private bool inputEnabled = true;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneReferencesSectionViewModel"/> class.</summary>
    /// <param name="commandService">The command service that persists scene-reference edits.</param>
    /// <param name="commandContextProvider">The current document command context.</param>
    /// <param name="assetProvider">The shared content catalog used by typed pickers.</param>
    /// <param name="observerScheduler">The scheduler used for catalog notifications.</param>
    internal SceneReferencesSectionViewModel(
        ISceneDocumentCommandService? commandService,
        Func<SceneDocumentCommandContext?>? commandContextProvider,
        IContentBrowserAssetProvider? assetProvider,
        IScheduler observerScheduler)
    {
        this.commandService = commandService;
        this.commandContextProvider = commandContextProvider;
        this.assetProvider = assetProvider;
        this.observerScheduler = observerScheduler;
        this.CatalogNotice = assetProvider is null ? "The shared content catalog is unavailable." : string.Empty;
    }

    [ObservableProperty]
    public partial ContentBrowserAssetItem? SelectedScript { get; set; }

    [ObservableProperty]
    public partial ContentBrowserAssetItem? SelectedInputAction { get; set; }

    [ObservableProperty]
    public partial ContentBrowserAssetItem? SelectedInputMappingContext { get; set; }

    [ObservableProperty]
    public partial ContentBrowserAssetItem? SelectedPhysicsSidecar { get; set; }

    [ObservableProperty]
    public partial string ExtraAssetText { get; set; } = string.Empty;

    /// <summary>Gets assignable script assets.</summary>
    public ObservableCollection<ContentBrowserAssetItem> ScriptCandidates { get; } = [];

    /// <summary>Gets assignable input action assets.</summary>
    public ObservableCollection<ContentBrowserAssetItem> InputActionCandidates { get; } = [];

    /// <summary>Gets assignable input mapping context assets.</summary>
    public ObservableCollection<ContentBrowserAssetItem> InputMappingContextCandidates { get; } = [];

    /// <summary>Gets assignable physics scene assets.</summary>
    public ObservableCollection<ContentBrowserAssetItem> PhysicsSidecarCandidates { get; } = [];

    /// <summary>Gets the scene's script references, including unavailable entries.</summary>
    public ObservableCollection<SceneReferenceRow> Scripts { get; } = [];

    /// <summary>Gets the scene's input action references, including unavailable entries.</summary>
    public ObservableCollection<SceneReferenceRow> InputActions { get; } = [];

    /// <summary>Gets the scene's input mapping context references, including unavailable entries.</summary>
    public ObservableCollection<SceneReferenceRow> InputMappingContexts { get; } = [];

    /// <summary>Gets the scene's physics sidecar references, including unavailable entries.</summary>
    public ObservableCollection<SceneReferenceRow> PhysicsSidecars { get; } = [];

    /// <summary>Gets the scene's additional native virtual asset paths.</summary>
    public ObservableCollection<ExtraAssetRow> ExtraAssets { get; } = [];

    /// <summary>Gets the current catalog availability message.</summary>
    public string CatalogNotice { get; private set; } = string.Empty;

    /// <summary>Gets whether a catalog availability message is present.</summary>
    public bool HasCatalogNotice => this.CatalogNotice.Length > 0;

    /// <summary>Gets the latest edit validation or persistence message.</summary>
    public string ErrorMessage { get; private set; } = string.Empty;

    /// <summary>Gets whether a typed reference or extra path can be authored.</summary>
    public bool CanEdit => !this.disposed && !this.isBusy && this.inputEnabled
        && this.scene is { } current
        && this.commandService is not null
        && this.commandContextProvider?.Invoke() is { } context
        && ReferenceEquals(context.Scene, current);

    /// <summary>Gets whether the extra-path input can be added.</summary>
    public bool CanAddExtraAsset => this.CanEdit && !string.IsNullOrWhiteSpace(this.ExtraAssetText);

    /// <summary>Gets whether catalog refresh can be requested.</summary>
    public bool CanRetryCatalog => !this.disposed && this.assetProvider is not null;

    /// <summary>Gets whether an edit error should be displayed.</summary>
    public bool HasError => this.ErrorMessage.Length > 0;

    /// <summary>Gets completion of any in-flight reference edit.</summary>
    internal Task Pending => this.pending;

    /// <summary>Gets a notification identity for searchable rows owned by the section.</summary>
    internal string SearchableContent => string.Empty;

    /// <summary>Binds the selected scene and starts the shared catalog feed.</summary>
    /// <param name="value">The current scene, or null when scene authoring is unavailable.</param>
    internal void Bind(Scene? value)
    {
        if (this.disposed || ReferenceEquals(this.scene, value))
        {
            return;
        }

        if (this.scene is { } previous)
        {
            previous.PropertyChanged -= this.OnSceneChanged;
        }

        this.scene = value;
        if (value is { } current)
        {
            current.PropertyChanged += this.OnSceneChanged;
        }

        this.StartAssets();
        this.RefreshFromScene();
        this.NotifyCanEditChanged();
    }

    /// <summary>Enables or disables authoring with the parent inspector.</summary>
    /// <param name="enabled">Whether edits are currently allowed.</param>
    internal void SetInputEnabled(bool enabled)
    {
        this.inputEnabled = enabled;
        this.NotifyCanEditChanged();
    }

    /// <summary>Gets whether the scene-reference section matches the active inspector search.</summary>
    /// <param name="query">The raw search query.</param>
    /// <returns><see langword="true"/> when all query tokens match section metadata or one asset row.</returns>
    internal bool MatchesSearch(string query)
    {
        var terms = query.Trim().Split([' ', '\t', '/', '_', '-'], StringSplitOptions.RemoveEmptyEntries)
            .Select(InspectorSearchModel.Normalize)
            .ToArray();
        if (terms.Length == 0)
        {
            return true;
        }

        var searchable = new List<string> { "Scene References Scripts Input Actions Input Mapping Contexts Physics Sidecars Extra Assets" };
        searchable.AddRange(this.Scripts.Select(static row => row.SearchText));
        searchable.AddRange(this.InputActions.Select(static row => row.SearchText));
        searchable.AddRange(this.InputMappingContexts.Select(static row => row.SearchText));
        searchable.AddRange(this.PhysicsSidecars.Select(static row => row.SearchText));
        searchable.AddRange(this.ExtraAssets.Select(static row => row.Path));
        searchable.AddRange(this.assets.Select(static asset => asset.DisplayName + " " + asset.DisplayPath));

        return searchable.Any(text =>
        {
            var normalized = InspectorSearchModel.Normalize(text);
            return terms.All(normalized.Contains);
        });
    }

    /// <summary>Adds the currently selected asset to its matching typed reference collection.</summary>
    /// <param name="kind">The typed collection to update.</param>
    /// <returns>The completion of the undoable and persisted edit.</returns>
    public Task AssignAsync(AssetKind kind)
    {
        var selected = kind switch
        {
            AssetKind.Script => this.SelectedScript,
            AssetKind.InputAction => this.SelectedInputAction,
            AssetKind.InputMappingContext => this.SelectedInputMappingContext,
            AssetKind.PhysicsScene => this.SelectedPhysicsSidecar,
            _ => null,
        };

        if (selected is null || selected.Kind != kind || !selected.IsSelectable || !this.CanEdit)
        {
            return Task.CompletedTask;
        }

        return this.ApplyAsync(this.WithTypedReference(this.scene!.References, kind, selected.IdentityUri, add: true));
    }

    /// <summary>Removes an existing typed reference without discarding unresolved references.</summary>
    /// <param name="row">The displayed reference to remove.</param>
    /// <returns>The completion of the undoable and persisted edit.</returns>
    public Task RemoveAsync(SceneReferenceRow row)
    {
        ArgumentNullException.ThrowIfNull(row);
        if (!this.CanEdit)
        {
            return Task.CompletedTask;
        }

        return this.ApplyAsync(this.WithTypedReference(this.scene!.References, row.Kind, row.Uri, add: false));
    }

    /// <summary>Adds the entered canonical native virtual path.</summary>
    /// <returns>The completion of the undoable and persisted edit.</returns>
    public async Task AddExtraAssetAsync()
    {
        if (!this.CanAddExtraAsset || this.scene is not { } current)
        {
            return;
        }

        var path = this.ExtraAssetText.Trim();
        var references = current.References;
        if (references.ExtraAssets.Contains(path, StringComparer.Ordinal))
        {
            this.SetErrorMessage($"Extra asset '{path}' is already referenced.");
            return;
        }

        await this.ApplyAsync(new()
        {
            Scripts = [.. references.Scripts],
            InputActions = [.. references.InputActions],
            InputMappingContexts = [.. references.InputMappingContexts],
            PhysicsSidecars = [.. references.PhysicsSidecars],
            ExtraAssets = [.. references.ExtraAssets, path],
        }).ConfigureAwait(true);
        if (this.ErrorMessage.Length == 0)
        {
            this.ExtraAssetText = string.Empty;
        }
    }

    /// <summary>Removes an additional native virtual path.</summary>
    /// <param name="path">The path to remove.</param>
    /// <returns>The completion of the undoable and persisted edit.</returns>
    public Task RemoveExtraAssetAsync(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        if (!this.CanEdit || this.scene is not { } current)
        {
            return Task.CompletedTask;
        }

        return this.ApplyAsync(new()
        {
            Scripts = [.. current.References.Scripts],
            InputActions = [.. current.References.InputActions],
            InputMappingContexts = [.. current.References.InputMappingContexts],
            PhysicsSidecars = [.. current.References.PhysicsSidecars],
            ExtraAssets = [.. current.References.ExtraAssets.Where(candidate => !string.Equals(candidate, path, StringComparison.Ordinal))],
        });
    }

    /// <summary>Refreshes the shared catalog for typed asset pickers.</summary>
    /// <param name="cancellationToken">The refresh cancellation token.</param>
    public async Task RefreshCatalogAsync(CancellationToken cancellationToken = default)
    {
        if (this.assetProvider is null)
        {
            this.SetCatalogNotice("The shared content catalog is unavailable.");
            return;
        }

        try
        {
            await this.assetProvider.RefreshAsync(AssetBrowserFilter.Default, cancellationToken).ConfigureAwait(true);
            this.SetCatalogNotice(string.Empty);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or ObjectDisposedException or OperationCanceledException)
        {
            if (exception is not OperationCanceledException || !cancellationToken.IsCancellationRequested)
            {
                this.SetCatalogNotice("Could not refresh the shared content catalog. " + exception.Message);
                Debug.WriteLine($"[SceneReferencesSectionViewModel] Catalog refresh failed: {exception}");
            }
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
        this.OnPropertyChanged(nameof(this.CanRetryCatalog));
        if (this.scene is { } current)
        {
            current.PropertyChanged -= this.OnSceneChanged;
        }

        this.scene = null;
        this.NotifyCanEditChanged();
    }

    partial void OnExtraAssetTextChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.CanAddExtraAsset));
    }

    private SceneReferencesData WithTypedReference(SceneReferencesData current, AssetKind kind, Uri uri, bool add)
    {
        static IList<Uri> Update(IList<Uri> existing, Uri value, bool shouldAdd)
            => shouldAdd
                ? existing.Contains(value) ? [.. existing] : [.. existing, value]
                : [.. existing.Where(candidate => candidate != value)];

        var result = new SceneReferencesData
        {
            Scripts = [.. current.Scripts],
            InputActions = [.. current.InputActions],
            InputMappingContexts = [.. current.InputMappingContexts],
            PhysicsSidecars = [.. current.PhysicsSidecars],
            ExtraAssets = [.. current.ExtraAssets],
        };

        return kind switch
        {
            AssetKind.Script => result with { Scripts = Update(current.Scripts, uri, add) },
            AssetKind.InputAction => result with { InputActions = Update(current.InputActions, uri, add) },
            AssetKind.InputMappingContext => result with { InputMappingContexts = Update(current.InputMappingContexts, uri, add) },
            AssetKind.PhysicsScene => result with { PhysicsSidecars = Update(current.PhysicsSidecars, uri, add) },
            _ => result,
        };
    }

    private async Task ApplyAsync(SceneReferencesData references)
    {
        if (!this.CanEdit || this.scene is not { } current
            || this.commandService is not { } service
            || this.commandContextProvider?.Invoke() is not { } context
            || !ReferenceEquals(context.Scene, current))
        {
            return;
        }

        this.isBusy = true;
        this.SetErrorMessage(string.Empty);
        this.NotifyCanEditChanged();
        try
        {
            var edit = service.EditSceneReferencesAsync(context, references);
            this.pending = edit;
            var result = await edit.ConfigureAwait(true);
            if (!result.Succeeded)
            {
                this.SetErrorMessage(result.ValidationMessage ?? "The scene references could not be saved.");
                return;
            }

            this.SetErrorMessage(string.Empty);
            this.RefreshFromScene();
        }
        finally
        {
            this.pending = Task.CompletedTask;
            this.isBusy = false;
            this.NotifyCanEditChanged();
        }
    }

    private void StartAssets()
    {
        if (this.assetProvider is null || this.assetSubscription is not null || this.disposed)
        {
            return;
        }

        this.assetSubscription = this.assetProvider.Items
            .ObserveOn(this.observerScheduler)
            .Subscribe(this.UpdateAssets, this.OnCatalogFeedError);
    }

    private void UpdateAssets(IReadOnlyList<ContentBrowserAssetItem> value)
    {
        if (this.disposed)
        {
            return;
        }

        this.assets.Clear();
        this.assets.AddRange(value);
        this.SetCatalogNotice(string.Empty);
        this.UpdateCandidates(AssetKind.Script, this.ScriptCandidates, this.SelectedScript, item => this.SelectedScript = item);
        this.UpdateCandidates(AssetKind.InputAction, this.InputActionCandidates, this.SelectedInputAction, item => this.SelectedInputAction = item);
        this.UpdateCandidates(AssetKind.InputMappingContext, this.InputMappingContextCandidates, this.SelectedInputMappingContext, item => this.SelectedInputMappingContext = item);
        this.UpdateCandidates(AssetKind.PhysicsScene, this.PhysicsSidecarCandidates, this.SelectedPhysicsSidecar, item => this.SelectedPhysicsSidecar = item);
        this.RefreshFromScene();
    }

    private void UpdateCandidates(
        AssetKind kind,
        ObservableCollection<ContentBrowserAssetItem> destination,
        ContentBrowserAssetItem? selected,
        Action<ContentBrowserAssetItem?> setSelected)
    {
        var candidates = this.assets.Where(asset => asset.Kind == kind && IsReferenceAssignable(asset))
            .OrderBy(static asset => asset.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ThenBy(static asset => asset.DisplayPath, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        destination.Clear();
        foreach (var asset in candidates)
        {
            destination.Add(asset);
        }

        setSelected(selected is null
            ? null
            : candidates.FirstOrDefault(candidate => candidate.IdentityUri == selected.IdentityUri));
    }

    private void RefreshFromScene()
    {
        var references = this.scene?.References ?? new SceneReferencesData();
        this.UpdateReferenceRows(AssetKind.Script, references.Scripts, this.Scripts);
        this.UpdateReferenceRows(AssetKind.InputAction, references.InputActions, this.InputActions);
        this.UpdateReferenceRows(AssetKind.InputMappingContext, references.InputMappingContexts, this.InputMappingContexts);
        this.UpdateReferenceRows(AssetKind.PhysicsScene, references.PhysicsSidecars, this.PhysicsSidecars);
        this.ExtraAssets.Clear();
        foreach (var path in references.ExtraAssets)
        {
            this.ExtraAssets.Add(new(path, this.CanEdit));
        }

        this.OnPropertyChanged(nameof(this.SearchableContent));
    }

    private void UpdateReferenceRows(AssetKind kind, IEnumerable<Uri> references, ObservableCollection<SceneReferenceRow> destination)
    {
        destination.Clear();
        foreach (var uri in references)
        {
            var asset = this.assets.FirstOrDefault(candidate => candidate.IdentityUri == uri || candidate.CookedUri == uri);
            var expected = asset is not null && asset.Kind == kind && IsReferenceAssignable(asset);
            var path = uri.IsAbsoluteUri ? uri.AbsolutePath : uri.OriginalString;
            var label = asset?.DisplayName ?? Path.GetFileNameWithoutExtension(path);
            destination.Add(new(kind, uri, label, path, !expected, this.CanEdit));
        }
    }

    private static bool IsReferenceAssignable(ContentBrowserAssetItem asset)
        => asset.IsSelectable && (asset.CookedMetadata is not null || asset.CookStatus?.HasAvailableOutput == true);

    private void OnSceneChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.scene)
            && (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(Scene.References), StringComparison.Ordinal)))
        {
            this.RefreshFromScene();
        }
    }

    private void OnCatalogFeedError(Exception exception)
    {
        this.SetCatalogNotice("The shared content catalog could not be read. " + exception.Message);
        Debug.WriteLine($"[SceneReferencesSectionViewModel] Catalog feed failed: {exception}");
    }

    private void SetCatalogNotice(string value)
    {
        if (string.Equals(this.CatalogNotice, value, StringComparison.Ordinal))
        {
            return;
        }

        this.CatalogNotice = value;
        this.OnPropertyChanged(nameof(this.CatalogNotice));
        this.OnPropertyChanged(nameof(this.HasCatalogNotice));
        this.OnPropertyChanged(nameof(this.CanRetryCatalog));
        this.OnPropertyChanged(nameof(this.SearchableContent));
    }

    private void SetErrorMessage(string value)
    {
        if (string.Equals(this.ErrorMessage, value, StringComparison.Ordinal))
        {
            return;
        }

        this.ErrorMessage = value;
        this.OnPropertyChanged(nameof(this.ErrorMessage));
        this.OnPropertyChanged(nameof(this.HasError));
    }

    private void NotifyCanEditChanged()
    {
        this.OnPropertyChanged(nameof(this.CanEdit));
        this.OnPropertyChanged(nameof(this.CanAddExtraAsset));
        for (var index = 0; index < this.Scripts.Count; index++)
        {
            this.Scripts[index] = this.Scripts[index] with { CanEdit = this.CanEdit };
        }

        for (var index = 0; index < this.InputActions.Count; index++)
        {
            this.InputActions[index] = this.InputActions[index] with { CanEdit = this.CanEdit };
        }

        for (var index = 0; index < this.InputMappingContexts.Count; index++)
        {
            this.InputMappingContexts[index] = this.InputMappingContexts[index] with { CanEdit = this.CanEdit };
        }

        for (var index = 0; index < this.PhysicsSidecars.Count; index++)
        {
            this.PhysicsSidecars[index] = this.PhysicsSidecars[index] with { CanEdit = this.CanEdit };
        }

        for (var index = 0; index < this.ExtraAssets.Count; index++)
        {
            this.ExtraAssets[index] = this.ExtraAssets[index] with { CanEdit = this.CanEdit };
        }
    }
}

/// <summary>Displays a typed scene reference and whether it is currently resolvable.</summary>
public sealed record SceneReferenceRow(AssetKind Kind, Uri Uri, string DisplayName, string DisplayPath, bool IsUnavailable, bool CanEdit)
{
    /// <summary>Gets searchable display text for this reference.</summary>
    public string SearchText => this.DisplayName + " " + this.DisplayPath;

    /// <summary>Gets the current resolution status.</summary>
    public string StatusText => this.IsUnavailable ? "Unavailable or wrong asset type" : "Resolved";
}

/// <summary>Displays an additional native virtual path with its current edit availability.</summary>
public sealed record ExtraAssetRow(string Path, bool CanEdit);
