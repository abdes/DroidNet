// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.SceneInspector.Presentation;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Owns typed scene asset-reference assignments and retained unresolved references.</summary>
public sealed partial class SceneReferencesSectionViewModel : ObservableObject, IDisposable
{
    private readonly Func<SceneAuthoring?>? authoringProvider;
    private readonly IAssetFeed? assetProvider;
    private readonly IScheduler observerScheduler;
    private readonly List<AssetEntry> assets = [];
    private IDisposable? assetSubscription;
    private Scene? scene;
    private Task pending = Task.CompletedTask;
    private bool isBusy;
    private bool inputEnabled = true;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="SceneReferencesSectionViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    /// <param name="assetProvider">The shared content catalog used by typed pickers.</param>
    /// <param name="observerScheduler">The scheduler used for catalog notifications.</param>
    internal SceneReferencesSectionViewModel(
        Func<SceneAuthoring?>? authoringProvider,
        IAssetFeed? assetProvider,
        IScheduler observerScheduler)
    {
        this.authoringProvider = authoringProvider;
        this.assetProvider = assetProvider;
        this.observerScheduler = observerScheduler;
        this.CatalogNotice = assetProvider is null ? "The shared content catalog is unavailable." : string.Empty;
    }

    [ObservableProperty]
    public partial string ExtraAssetText { get; set; } = string.Empty;

    /// <summary>Gets the script assets the scene can reference, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel ScriptChoices { get; } = new(static asset => AssetChoice.For(asset, "Script", "\uE943"));

    /// <summary>Gets the input action assets the scene can reference, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel InputActionChoices { get; } = new(static asset => AssetChoice.For(asset, "Input action", "\uE765"));

    /// <summary>Gets the physics scene assets the scene can reference, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel PhysicsSidecarChoices { get; } = new(static asset => AssetChoice.For(asset, "Physics sidecar", "\uE945"));

    /// <summary>Gets the scene's script references, including unavailable entries.</summary>
    public ObservableCollection<SceneReferenceRow> Scripts { get; } = [];

    /// <summary>Gets the scene's input action references, including unavailable entries.</summary>
    public ObservableCollection<SceneReferenceRow> InputActions { get; } = [];

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
        && this.authoringProvider?.Invoke() is { } authoring
        && ReferenceEquals(authoring.Scene, current);

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

        var searchable = new List<string> { "Scene References Scripts Input Actions Physics Sidecars Extra Assets" };
        searchable.AddRange(this.Scripts.Select(static row => row.SearchText));
        searchable.AddRange(this.InputActions.Select(static row => row.SearchText));
        searchable.AddRange(this.PhysicsSidecars.Select(static row => row.SearchText));
        searchable.AddRange(this.ExtraAssets.Select(static row => row.Path));
        searchable.AddRange(this.assets.Select(static asset => asset.DisplayName + " " + asset.DisplayPath));

        return searchable.Any(text =>
        {
            var normalized = InspectorSearchModel.Normalize(text);
            return terms.All(normalized.Contains);
        });
    }

    /// <summary>Adds a picked asset to its matching typed reference collection.</summary>
    /// <param name="kind">The typed collection to update.</param>
    /// <param name="asset">The picked asset; one the collection does not accept is ignored.</param>
    /// <returns>The completion of the undoable and persisted edit.</returns>
    public Task AssignAsync(AssetKind kind, AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var picked = this.assets.FirstOrDefault(candidate => candidate.Identity == asset);
        if (picked is null || !AssetRule.ForCooked(kind).Accepts(picked) || !this.CanEdit)
        {
            return Task.CompletedTask;
        }

        return this.ApplyAsync(this.WithTypedReference(this.scene!.References, kind, asset, add: true));
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

        return this.ApplyAsync(this.WithTypedReference(this.scene!.References, row.Kind, row.Asset, add: false));
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
        if (!AssetPath.TryParse(path, out var asset) || asset.IsMount)
        {
            this.SetErrorMessage($"Extra asset path '{path}' must be an absolute canonical virtual path.");
            return;
        }

        var references = current.References;
        if (references.ExtraAssets.Contains(asset))
        {
            this.SetErrorMessage($"Extra asset '{path}' is already referenced.");
            return;
        }

        await this.ApplyAsync(new()
        {
            Scripts = [.. references.Scripts],
            InputActions = [.. references.InputActions],
            PhysicsSidecars = [.. references.PhysicsSidecars],
            ExtraAssets = [.. references.ExtraAssets, asset],
        }).ConfigureAwait(true);
        if (this.ErrorMessage.Length == 0)
        {
            this.ExtraAssetText = string.Empty;
        }
    }

    /// <summary>Removes an additional asset.</summary>
    /// <param name="asset">The asset to remove.</param>
    /// <returns>The completion of the undoable and persisted edit.</returns>
    public Task RemoveExtraAssetAsync(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        if (!this.CanEdit || this.scene is not { } current)
        {
            return Task.CompletedTask;
        }

        return this.ApplyAsync(new()
        {
            Scripts = [.. current.References.Scripts],
            InputActions = [.. current.References.InputActions],
            PhysicsSidecars = [.. current.References.PhysicsSidecars],
            ExtraAssets = [.. current.References.ExtraAssets.Where(candidate => candidate != asset)],
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
            await this.assetProvider.RefreshAsync(cancellationToken).ConfigureAwait(true);
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

    private SceneReferencesData WithTypedReference(SceneReferencesData current, AssetKind kind, AssetPath uri, bool add)
    {
        static IList<AssetPath> Update(IList<AssetPath> existing, AssetPath value, bool shouldAdd)
            => shouldAdd
                ? existing.Contains(value) ? [.. existing] : [.. existing, value]
                : [.. existing.Where(candidate => candidate != value)];

        var result = new SceneReferencesData
        {
            Scripts = [.. current.Scripts],
            InputActions = [.. current.InputActions],
            PhysicsSidecars = [.. current.PhysicsSidecars],
            ExtraAssets = [.. current.ExtraAssets],
        };

        return kind switch
        {
            AssetKind.Script => result with { Scripts = Update(current.Scripts, uri, add) },
            AssetKind.InputAction => result with { InputActions = Update(current.InputActions, uri, add) },
            AssetKind.PhysicsScene => result with { PhysicsSidecars = Update(current.PhysicsSidecars, uri, add) },
            _ => result,
        };
    }

    private async Task ApplyAsync(SceneReferencesData references)
    {
        if (!this.CanEdit || this.scene is not { } current
            || this.authoringProvider?.Invoke() is not { } authoring
            || !ReferenceEquals(authoring.Scene, current))
        {
            return;
        }

        this.isBusy = true;
        this.SetErrorMessage(string.Empty);
        this.NotifyCanEditChanged();
        try
        {
            var edit = authoring.ExecuteAsync(new SetSceneReferences(references));
            this.pending = edit;
            var result = await edit.ConfigureAwait(true);
            if (!result.Succeeded)
            {
                this.SetErrorMessage(result.Diagnostics.FirstOrDefault(static diagnostic => diagnostic.IsError)?.Message ?? "The scene references could not be saved.");
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

    private void UpdateAssets(IReadOnlyList<AssetEntry> value)
    {
        if (this.disposed)
        {
            return;
        }

        this.assets.Clear();
        this.assets.AddRange(value);
        this.SetCatalogNotice(string.Empty);
        this.ScriptChoices.Update(AssetRule.ForCooked(AssetKind.Script).Apply(this.assets));
        this.InputActionChoices.Update(AssetRule.ForCooked(AssetKind.InputAction).Apply(this.assets));
        this.PhysicsSidecarChoices.Update(AssetRule.ForCooked(AssetKind.PhysicsScene).Apply(this.assets));
        this.RefreshFromScene();
    }

    private void RefreshFromScene()
    {
        var references = this.scene?.References ?? new SceneReferencesData();
        this.UpdateReferenceRows(AssetKind.Script, references.Scripts, this.Scripts);
        this.UpdateReferenceRows(AssetKind.InputAction, references.InputActions, this.InputActions);
        this.UpdateReferenceRows(AssetKind.PhysicsScene, references.PhysicsSidecars, this.PhysicsSidecars);
        this.ExtraAssets.Clear();
        foreach (var asset in references.ExtraAssets)
        {
            this.ExtraAssets.Add(new(asset, this.CanEdit));
        }

        this.OnPropertyChanged(nameof(this.SearchableContent));
    }

    private void UpdateReferenceRows(AssetKind kind, IEnumerable<AssetPath> references, ObservableCollection<SceneReferenceRow> destination)
    {
        destination.Clear();
        foreach (var reference in references)
        {
            var asset = this.assets.FirstOrDefault(candidate => candidate.Identity == reference);
            var expected = asset is not null && AssetRule.ForCooked(kind).Accepts(asset);
            var label = asset?.DisplayName ?? Path.GetFileNameWithoutExtension(reference.Name);
            destination.Add(new(kind, reference, label, reference.VirtualPath, !expected, this.CanEdit));
        }
    }

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
public sealed record SceneReferenceRow(AssetKind Kind, AssetPath Asset, string DisplayName, string DisplayPath, bool IsUnavailable, bool CanEdit)
{
    /// <summary>Gets searchable display text for this reference.</summary>
    public string SearchText => this.DisplayName + " " + this.DisplayPath;

    /// <summary>Gets the current resolution status.</summary>
    public string StatusText => this.IsUnavailable ? "Unavailable or wrong asset type" : "Resolved";
}

/// <summary>Displays an additional asset with its current edit availability.</summary>
/// <param name="Asset">The asset.</param>
/// <param name="CanEdit">Whether the row can be removed.</param>
public sealed record ExtraAssetRow(AssetPath Asset, bool CanEdit)
{
    /// <summary>Gets the asset's virtual path, as the row shows it.</summary>
    public string Path => this.Asset.VirtualPath;
}
