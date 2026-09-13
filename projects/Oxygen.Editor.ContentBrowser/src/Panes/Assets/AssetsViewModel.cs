// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DroidNet.Storage;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Import;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
///     The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
/// <param name="cookRuns">The session cooking controls.</param>
/// <param name="assetCatalog">The asset catalog.</param>
/// <param name="vmToViewConverter">The converter for converting view models to views.</param>
/// <param name="contentBrowserState">The content browser state to track selection changes.</param>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="projectManagerService">The project manager service for creating scenes.</param>
/// <param name="contentPipelineService">The explicit editor content-pipeline service.</param>
/// <param name="assetProvider">The shared content-browser asset provider.</param>
/// <param name="operationResults">The operation-result publisher.</param>
/// <param name="statusReducer">The operation status reducer.</param>
/// <param name="storage">The storage provider.</param>
/// <param name="importService">The import service.</param>
/// <param name="windowManagerService">The window manager service.</param>
public partial class AssetsViewModel(
    Oxygen.Editor.ContentPipeline.Cooking.ICookRunService cookRuns,
    IAssetCatalog assetCatalog,
    ViewModelToView vmToViewConverter,
    ContentBrowserState contentBrowserState,
    IProjectContextService projectContextService,
    IProjectManagerService projectManagerService,
    IAuthoringTargetResolver authoringTargetResolver,
    IContentPipelineService contentPipelineService,
    IContentBrowserAssetProvider assetProvider,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    IStorageProvider storage,
    IMessenger messenger,
    IImportService importService,
    IWindowManagerService windowManagerService) : AbstractOutletContainer, IRoutingAware
{
    private bool disposed;

    private bool isInitialized;

    /// <summary>Gets the shared session controls for automatic cooking.</summary>
    public Oxygen.Editor.ContentPipeline.Cooking.ICookRunService CookRuns { get; } = cookRuns;

    /// <summary>
    ///     Gets the converter for converting view models to views.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; } = vmToViewConverter;

    /// <summary>
    ///     Gets the layout view model.
    /// </summary>
    public object? LayoutViewModel => this.Outlets["right"].viewModel;

    [ObservableProperty]
    public partial bool IsOperationResultVisible { get; set; }

    [ObservableProperty]
    public partial string OperationResultTitle { get; set; } = string.Empty;

    [ObservableProperty]
    public partial string OperationResultMessage { get; set; } = string.Empty;

    [ObservableProperty]
    public partial InfoBarSeverity OperationResultSeverity { get; set; } = InfoBarSeverity.Informational;

    /// <summary>Gets the visibility of cooking for the selected authored input.</summary>
    public Visibility CookSelectedAssetVisibility => this.CanCookSelectedAsset() ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the visibility of cooking for the selected authored folder.</summary>
    public Visibility CookSelectedFolderVisibility => this.CanCookSelectedFolder() ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Maps a browser folder to an authored material destination.</summary>
    /// <param name="selected">The selected folder.</param>
    /// <param name="project">Optional project mount declarations.</param>
    /// <returns>The normalized virtual material folder.</returns>
    public static string NormalizeMaterialFolder(string? selected, ProjectContext? project = null)
    {
        if (string.IsNullOrWhiteSpace(selected))
        {
            return "/Content/Materials";
        }

        var normalized = selected.Replace('\\', '/').Trim();
        normalized = normalized.TrimEnd('/');
        var normalizedNoRoot = normalized.TrimStart('/');

        if (TryMapSelectedAuthoringFolder(project, normalizedNoRoot, out var mapped))
        {
            return mapped;
        }

        if (normalized.StartsWith('/'))
        {
            return normalized.Equals("/Content", StringComparison.OrdinalIgnoreCase)
                ? "/Content/Materials"
                : normalized.Equals("/Content/Materials", StringComparison.OrdinalIgnoreCase)
                  || normalized.StartsWith("/Content/Materials/", StringComparison.OrdinalIgnoreCase)
                ? normalized
                : "/Content/Materials";
        }

        normalized = normalized.TrimStart('/');
        return normalized.Equals("Content", StringComparison.OrdinalIgnoreCase)
            ? "/Content/Materials"
            : normalized.Equals("Content/Materials", StringComparison.OrdinalIgnoreCase)
              || normalized.StartsWith("Content/Materials/", StringComparison.OrdinalIgnoreCase)
                ? "/" + normalized
                : "/Content/Materials";
    }

    /// <inheritdoc />
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        // One-time initialization for singleton
        if (!this.isInitialized)
        {
            this.Outlets.Add("right", (nameof(this.LayoutViewModel), null));

            this.PropertyChanging += this.OnLayoutViewModelChanging;
            this.PropertyChanged += this.OnLayoutViewModelChanged;

            // Listen for changes to ContentBrowserState selection via PropertyChanged
            contentBrowserState.PropertyChanged += this.OnContentBrowserStatePropertyChanged;

            messenger.Register<AssetsChangedMessage>(this, (_, _) => this.OnAssetsChanged());

            // Indexing is started by ContentBrowserViewModel - no need to start here
            this.isInitialized = true;
        }

        // Asset indexing runs automatically in background with file watching
    }

    /// <summary>Requests an authored material in the resolved destination.</summary>
    /// <param name="materialName">The proposed material name.</param>
    /// <param name="virtualFolder">The selected virtual folder.</param>
    /// <returns>The creation request dispatch.</returns>
    public Task CreateNewMaterialAsync(string materialName, string virtualFolder)
    {
        if (!TryNormalizeMaterialName(materialName, out var normalizedName))
        {
            Debug.WriteLine($"[AssetsViewModel] Rejected invalid material name '{materialName}'.");
            return Task.CompletedTask;
        }

        var folder = this.ResolveMaterialFolder(virtualFolder);
        var materialUri = new Uri($"{AssetUris.Scheme}://{folder.TrimEnd('/')}/{normalizedName}.omat.json");
        _ = messenger.Send(new CreateMaterialRequestMessage(materialUri, normalizedName));
        Debug.WriteLine($"[AssetsViewModel] Requested material creation {materialUri}");
        return Task.CompletedTask;
    }

    /// <summary>Chooses an unused material name in the resolved folder.</summary>
    /// <param name="virtualFolder">The selected virtual folder.</param>
    /// <returns>An available default material name.</returns>
    public string CreateDefaultMaterialName(string virtualFolder)
    {
        var folder = this.ResolveMaterialFolder(virtualFolder);
        var count = this.LayoutViewModel is AssetsLayoutViewModel layout
            ? layout.Assets.Count(asset => asset.Kind == AssetKind.Material)
            : 0;
        var start = Math.Max(1, count + 1);
        for (var i = start; i < start + 1000; i++)
        {
            var candidate = string.Create(CultureInfo.InvariantCulture, $"NewMaterial{i}");
            if (!this.MaterialSourceExists(folder, candidate))
            {
                return candidate;
            }
        }

        return string.Create(CultureInfo.InvariantCulture, $"NewMaterial{Guid.NewGuid():N}");
    }

    /// <summary>Gets the authoring destination for a new material.</summary>
    /// <returns>The resolved virtual folder.</returns>
    public string GetSelectedMaterialFolder()
        => this.ResolveMaterialFolder(contentBrowserState.SelectedFolders.FirstOrDefault());

    /// <summary>Retains the selected local mount when resolving a creation target.</summary>
    /// <param name="activeProject">The active project.</param>
    /// <param name="selected">The current folder selection.</param>
    /// <returns>The authoring target selection.</returns>
    internal static ContentBrowserSelection CreateMaterialTargetSelection(ProjectContext activeProject, string? selected)
    {
        var normalized = selected?.Replace('\\', '/').Trim().Trim('/');
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return new ContentBrowserSelection(selected);
        }

        var firstSlash = normalized.IndexOf('/', StringComparison.Ordinal);
        var root = firstSlash < 0 ? normalized : normalized[..firstSlash];
        var localMount = activeProject.LocalFolderMounts.FirstOrDefault(mount =>
            string.Equals(mount.Name, root, StringComparison.OrdinalIgnoreCase));

        return new ContentBrowserSelection(selected, localMount?.Name);
    }

    /// <summary>Combines an operation summary with its diagnostic details.</summary>
    /// <param name="message">The summary.</param>
    /// <param name="diagnostics">The operation diagnostics.</param>
    /// <returns>The combined feedback message.</returns>
    internal static string BuildOperationMessage(string message, IReadOnlyList<DiagnosticRecord> diagnostics)
    {
        var diagnostic = diagnostics.FirstOrDefault(static item =>
            item.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal)
                         ?? diagnostics.FirstOrDefault(static item => item.Severity == DiagnosticSeverity.Warning);
        if (diagnostic is null)
        {
            return message;
        }

        var details = diagnostic.Message;
        return string.IsNullOrWhiteSpace(details)
            || string.Equals(details, message, StringComparison.Ordinal)
            || message.Contains(details, StringComparison.Ordinal)
            ? message
            : $"{message} {details}";
    }

    /// <summary>Identifies explicit inspection operations whose successful result remains visible.</summary>
    /// <param name="operationKind">The operation kind.</param>
    /// <returns>Whether the browser displays successful feedback.</returns>
    internal static bool ShouldShowSucceededOperationResult(string operationKind)
        => string.Equals(operationKind, ContentPipelineOperationKinds.CookedOutputInspect, StringComparison.Ordinal)
           || string.Equals(operationKind, ContentPipelineOperationKinds.CookedOutputValidate, StringComparison.Ordinal);

    /// <summary>
    ///     Releases the unmanaged resources used by the <see cref="AssetsViewModel" /> and optionally releases the managed
    ///     resources.
    /// </summary>
    /// <param name="disposing">
    ///     true to release both managed and unmanaged resources; false to release only unmanaged
    ///     resources.
    /// </param>
    protected override void Dispose(bool disposing)
    {
        if (!this.disposed)
        {
            if (disposing)
            {
                messenger.UnregisterAll(this);

                // Cleanup event subscriptions
                contentBrowserState.PropertyChanged -= this.OnContentBrowserStatePropertyChanged;
                this.PropertyChanging -= this.OnLayoutViewModelChanging;
                this.PropertyChanged -= this.OnLayoutViewModelChanged;

                // Cleanup layout view model if necessary
                if (this.isInitialized && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
                {
                    layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
                    layoutViewModel.PropertyChanged -= this.OnAssetSelectionChanged;
                }
            }

            this.disposed = true;
        }

        base.Dispose(disposing);
    }

    private static bool TryMapSelectedAuthoringFolder(ProjectContext? project, string normalizedNoRoot, out string virtualFolder)
    {
        virtualFolder = string.Empty;
        if (project is null || string.IsNullOrWhiteSpace(normalizedNoRoot))
        {
            return false;
        }

        foreach (var mount in project.AuthoringMounts.OrderByDescending(static mount => mount.RelativePath.Length))
        {
            var mountFolder = mount.RelativePath.Replace('\\', '/').Trim('/');
            if (string.IsNullOrWhiteSpace(mountFolder))
            {
                continue;
            }

            if (normalizedNoRoot.Equals(mountFolder, StringComparison.OrdinalIgnoreCase))
            {
                virtualFolder = "/" + mount.Name + "/Materials";
                return true;
            }

            if (normalizedNoRoot.StartsWith(mountFolder + "/", StringComparison.OrdinalIgnoreCase))
            {
                var mountRelative = normalizedNoRoot[(mountFolder.Length + 1)..];
                virtualFolder = mountRelative.Equals("Materials", StringComparison.OrdinalIgnoreCase)
                                || mountRelative.StartsWith("Materials/", StringComparison.OrdinalIgnoreCase)
                    ? "/" + mount.Name + "/" + mountRelative
                    : "/" + mount.Name + "/Materials";
                return true;
            }
        }

        return false;
    }

    private static bool TryNormalizeMaterialName(string materialName, out string normalized)
    {
        normalized = materialName.Trim();
        if (normalized.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase))
        {
            normalized = normalized[..^".omat.json".Length];
        }
        else if (normalized.EndsWith(".omat", StringComparison.OrdinalIgnoreCase))
        {
            normalized = normalized[..^".omat".Length];
        }

        if (string.IsNullOrWhiteSpace(normalized)
            || normalized.Contains('/', StringComparison.Ordinal)
            || normalized.Contains('\\', StringComparison.Ordinal)
            || string.Equals(normalized, ".", StringComparison.Ordinal)
            || string.Equals(normalized, "..", StringComparison.Ordinal)
            || normalized.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            normalized = string.Empty;
            return false;
        }

        return true;
    }

    private static List<DiagnosticRecord> NormalizeDiagnostics(
        Guid operationId,
        IEnumerable<DiagnosticRecord> diagnostics)
        => diagnostics
            .Select(diagnostic => diagnostic.OperationId == operationId
                ? diagnostic
                : diagnostic with { OperationId = operationId })
            .ToList();

    private static string DescribeScope(Uri? scopeUri)
        => scopeUri?.ToString() ?? "the active project";

    private static List<DiagnosticRecord> ToDiagnosticRecords(
        Guid operationId,
        IReadOnlyList<ImportDiagnostic> diagnostics)
        => diagnostics.Select(diagnostic => new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.AssetImport,
                Severity = diagnostic.Severity switch
                {
                    ImportDiagnosticSeverity.Error => DiagnosticSeverity.Error,
                    ImportDiagnosticSeverity.Warning => DiagnosticSeverity.Warning,
                    ImportDiagnosticSeverity.Info => DiagnosticSeverity.Info,
                    _ => DiagnosticSeverity.Info,
                },
                Code = string.IsNullOrWhiteSpace(diagnostic.Code)
                    ? AssetImportDiagnosticCodes.ImportFailed
                    : diagnostic.Code,
                Message = diagnostic.Message,
                AffectedPath = diagnostic.SourcePath,
                AffectedVirtualPath = diagnostic.VirtualPath,
            })
            .ToList();

    private void OnAssetsChanged()
    {
        if (this.LayoutViewModel is AssetsLayoutViewModel layout)
        {
            _ = layout.RefreshAsync();
        }
    }

    private async void OnContentBrowserStatePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ContentBrowserState.SelectedFolders), StringComparison.Ordinal))
        {
            this.NotifyCookSelection();
            Debug.WriteLine(
                $"[AssetsViewModel] ContentBrowserState.SelectedFolders changed. Selected folders: [{string.Join(", ", contentBrowserState.SelectedFolders)}]");

            // Asset indexing runs automatically in background - no manual refresh needed
            var assetCount = await assetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All)).ConfigureAwait(false);
            Debug.WriteLine($"[AssetsViewModel] Assets available: {assetCount.Count}");
        }
    }

    private async void OnAssetItemInvoked(object? sender, AssetsViewItemInvokedEventArgs args)
    {
        _ = sender; // Unused

        Debug.WriteLine(
            $"[AssetsViewModel] Item invoked: {args.InvokedItem.DisplayName}, Kind: {args.InvokedItem.Kind}, URI: {args.InvokedItem.IdentityUri}");

        if (args.InvokedItem.Kind == AssetKind.Scene)
        {
            var currentProject = projectManagerService.CurrentProject;
            if (currentProject is null)
            {
                return;
            }

            // Update the scene explorer
            var scene = currentProject.Scenes.FirstOrDefault(scene =>
                string.Equals(scene.Name, args.InvokedItem.DisplayName, StringComparison.OrdinalIgnoreCase));
            if (scene is not null)
            {
                currentProject.ActiveScene = scene;

                // Request to open the scene document
                _ = messenger.Send(new OpenSceneRequestMessage(scene));
            }
        }
        else if (args.InvokedItem.Kind == AssetKind.Folder)
        {
            // Navigate into the folder
            Debug.WriteLine($"[AssetsViewModel] Navigating to folder: {args.InvokedItem.DisplayPath}");
            await this.NavigateToFolder(args.InvokedItem.DisplayPath).ConfigureAwait(false);
        }
        else if (args.InvokedItem.Kind == AssetKind.Material
                 && args.InvokedItem.IdentityUri.AbsolutePath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase))
        {
            _ = messenger.Send(new OpenMaterialRequestMessage(args.InvokedItem.IdentityUri, args.InvokedItem.DisplayName));
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The view owns this operation boundary and reports failures.")]
    private async Task NavigateToFolder(string folderPath)
    {
        Debug.WriteLine($"[AssetsViewModel] NavigateToFolder called with: {folderPath}");

        try
        {
            var folder = await storage.GetFolderFromPathAsync(folderPath).ConfigureAwait(true);

            Debug.WriteLine($"[AssetsViewModel] Requesting navigation to folder: {folder.Location}");

            // Request navigation via the messenger. This allows ProjectLayoutViewModel
            // to handle the navigation, ensuring correct virtual path resolution.
            _ = messenger.Send(new NavigateToFolderRequestMessage(folder));
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[AssetsViewModel] Error navigating to folder '{folderPath}': {ex.Message}");
        }
    }

    /// <summary>
    ///     Creates a new scene with the specified name.
    /// </summary>
    /// <param name="sceneName">The name of the new scene.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    [RelayCommand]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The view owns this operation boundary and reports failures.")]
    private async Task CreateNewSceneAsync(string? sceneName)
    {
        if (string.IsNullOrWhiteSpace(sceneName))
        {
            // TODO: Show validation error or prompt for name
            return;
        }

        try
        {
            var newScene = await projectManagerService.CreateSceneAsync(sceneName).ConfigureAwait(true);
            if (newScene is not null)
            {
                projectContextService.Activate(ProjectContext.FromProject(newScene.Project));
                _ = messenger.Send(new AssetsChangedMessage());
                _ = messenger.Send(new OpenSceneRequestMessage(newScene));
            }

            // TODO: Show error message to user about scene creation failure
        }
        catch (Exception ex)
        {
            // TODO: Show error message to user
            Debug.WriteLine($"Failed to create scene '{sceneName}': {ex.Message}");
        }
    }

    /// <summary>
    ///     Handles the creation of a new scene by prompting for a name.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    [RelayCommand]
    private async Task CreateNewSceneWithPromptAsync()
    {
        var sceneCount = projectContextService.ActiveProject?.Scenes.Count ?? 0;
        var defaultName = string.Create(CultureInfo.InvariantCulture, $"NewScene{sceneCount + 1}");

        await this.CreateNewSceneAsync(defaultName).ConfigureAwait(true);
    }

    private string ResolveMaterialFolder(string? selected)
    {
        var activeProject = projectContextService.ActiveProject;
        if (activeProject is null)
        {
            return NormalizeMaterialFolder(selected);
        }

        var target = authoringTargetResolver.ResolveCreateTarget(
            activeProject,
            AuthoringAssetKind.Material,
            CreateMaterialTargetSelection(activeProject, selected));
        return target.FolderAssetUri.AbsolutePath;
    }

    private void OnLayoutViewModelChanging(object? sender, PropertyChangingEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
            layoutViewModel.PropertyChanged -= this.OnAssetSelectionChanged;
        }
    }

    private bool MaterialSourceExists(string virtualFolder, string materialName)
    {
        if (projectContextService.ActiveProject is not { } project || string.IsNullOrWhiteSpace(project.ProjectRoot))
        {
            return false;
        }

        var normalized = virtualFolder.Trim('/').Replace('\\', '/');
        var slash = normalized.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            return false;
        }

        var mountName = normalized[..slash];
        var relativeFolder = normalized[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            return false;
        }

        var path = Path.Combine(project.ProjectRoot, mount.RelativePath, relativeFolder, materialName + ".omat.json");
        return File.Exists(path);
    }

    private void OnLayoutViewModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked += this.OnAssetItemInvoked;
            layoutViewModel.PropertyChanged += this.OnAssetSelectionChanged;
            this.NotifyCookSelection();
        }
    }

    private bool CanCookSelectedAsset()
        => !this.disposed && this.isInitialized && this.LayoutViewModel is AssetsLayoutViewModel { SelectedAsset.CanCook: true };

    private bool CanCookSelectedFolder()
    {
        if (this.disposed || !this.isInitialized || projectContextService.ActiveProject is not { } project)
        {
            return false;
        }

        var path = this.GetSelectedFolderUri().AbsolutePath.TrimEnd('/');
        return project.AuthoringMounts.Any(mount => string.Equals(path, "/" + mount.Name, StringComparison.OrdinalIgnoreCase)
            || path.StartsWith("/" + mount.Name + "/", StringComparison.OrdinalIgnoreCase));
    }

    private void OnAssetSelectionChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(AssetsLayoutViewModel.SelectedAsset), StringComparison.Ordinal))
        {
            this.NotifyCookSelection();
        }
    }

    private void NotifyCookSelection()
    {
        this.CookSelectedAssetCommand.NotifyCanExecuteChanged();
        this.CookSelectedFolderCommand.NotifyCanExecuteChanged();
        this.OnPropertyChanged(nameof(this.CookSelectedAssetVisibility));
        this.OnPropertyChanged(nameof(this.CookSelectedFolderVisibility));
    }

    [RelayCommand(CanExecute = nameof(CanCookSelectedAsset))]
    private async Task CookSelectedAssetAsync()
    {
        if (this.LayoutViewModel is not AssetsLayoutViewModel { SelectedAsset: { } asset }
            || asset.Kind == AssetKind.Folder)
        {
            this.PublishFailure(
                ContentPipelineOperationKinds.CookAsset,
                "No asset selected",
                "Select one cookable asset before running Cook Asset.",
                AssetCookDiagnosticCodes.CookFailed,
                scopeUri: null);
            return;
        }

        if (!asset.CanCook)
        {
            this.PublishFailure(
                ContentPipelineOperationKinds.CookAsset,
                "Cook Asset",
                "Select an authored descriptor asset, not cooked output.",
                AssetCookDiagnosticCodes.CookFailed,
                asset.IdentityUri);
            return;
        }

        await this.RunCookAsync(
                ContentPipelineOperationKinds.CookAsset,
                "Cook Asset",
                asset.IdentityUri,
                () => contentPipelineService.CookAssetAsync(asset.IdentityUri, CancellationToken.None))
            .ConfigureAwait(true);
    }

    [RelayCommand(CanExecute = nameof(CanCookSelectedFolder))]
    private async Task CookSelectedFolderAsync()
        => await this.RunCookAsync(
                ContentPipelineOperationKinds.CookFolder,
                "Cook Folder",
                this.GetSelectedFolderUri(),
                () => contentPipelineService.CookFolderAsync(this.GetSelectedFolderUri(), CancellationToken.None))
            .ConfigureAwait(true);

    [RelayCommand]
    private async Task CookProjectAsync()
        => await this.RunCookAsync(
                ContentPipelineOperationKinds.CookProject,
                "Cook Project",
                scopeUri: null,
                () => contentPipelineService.CookProjectAsync(CancellationToken.None))
            .ConfigureAwait(true);

    [RelayCommand]
    private Task InspectCookedOutputAsync() => this.OpenInspectionAsync(validate: false);

    [RelayCommand]
    private Task ValidateCookedOutputAsync() => this.OpenInspectionAsync(validate: true);

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI reports a document-opening failure at its operation boundary.")]
    private async Task OpenInspectionAsync(bool validate)
    {
        var scopeUri = this.GetSelectedFolderUri();
        try
        {
            if (projectContextService.ActiveProject is not { } project)
            {
                return;
            }

            var request = messenger.Send(new OpenCookedInspectionRequestMessage(project, scopeUri, validate));
            if (!request.HasReceivedResponse || !await request.Response.ConfigureAwait(true))
            {
                throw new InvalidOperationException("The inspection document could not be opened in this workspace.");
            }
        }
        catch (Exception ex)
        {
            this.PublishFailure(
                ContentPipelineOperationKinds.CookedOutputInspect,
                "Inspect Cooked Output",
                ex.Message,
                ContentPipelineDiagnosticCodes.InspectFailed,
                scopeUri,
                ex);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The view owns this operation boundary and reports failures.")]
    private async Task RunCookAsync(
        string operationKind,
        string title,
        Uri? scopeUri,
        Func<Task<ContentCookResult>> cook)
    {
        this.IsOperationResultVisible = false;
        try
        {
            var result = await cook().ConfigureAwait(true);
            this.PublishCookResult(operationKind, title, result, scopeUri);
        }
        catch (Exception ex)
        {
            this.PublishFailure(
                operationKind,
                title,
                ex.Message,
                AssetCookDiagnosticCodes.CookFailed,
                scopeUri,
                ex,
                showInBrowser: false);
        }
    }

    private void PublishCookResult(
        string operationKind,
        string title,
        ContentCookResult result,
        Uri? scopeUri)
    {
        var cookedRoot = result.Validation?.CookedRoot ?? result.Inspection?.CookedRoot ?? "(no cooked root)";
        var message = result.Status == OperationStatus.Failed
            ? BuildOperationMessage($"Cook failed for {DescribeScope(scopeUri)}.", result.Diagnostics)
            : $"Cooked {result.CookedAssets.Count} assets to {cookedRoot}.";
        this.PublishOperation(
            result.OperationId,
            operationKind,
            result.Status,
            title,
            message,
            result.Diagnostics,
            scopeUri,
            showInBrowser: false);
    }

    private void PublishFailure(
        string operationKind,
        string title,
        string message,
        string code,
        Uri? scopeUri,
        Exception? exception = null,
        bool showInBrowser = true)
    {
        var operationId = Guid.NewGuid();
        var scope = this.CreateAffectedScope(scopeUri);
        var diagnostic = new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.ContentPipeline,
            Severity = DiagnosticSeverity.Error,
            Code = code,
            Message = message,
            TechnicalMessage = exception?.Message,
            ExceptionType = exception?.GetType().FullName,
            AffectedEntity = scope,
        };
        this.PublishOperation(operationId, operationKind, OperationStatus.Failed, title, message, [diagnostic], scopeUri, showInBrowser);
    }

    private void PublishOperation(
        Guid operationId,
        string operationKind,
        OperationStatus status,
        string title,
        string message,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        Uri? scopeUri,
        bool showInBrowser = true)
    {
        var normalizedDiagnostics = NormalizeDiagnostics(operationId, diagnostics);
        var severity = status == OperationStatus.Failed && normalizedDiagnostics.Count == 0
            ? DiagnosticSeverity.Error
            : statusReducer.ComputeSeverity(normalizedDiagnostics);
        var result = new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = status,
            Severity = severity,
            Title = title,
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = this.CreateAffectedScope(scopeUri),
            Diagnostics = normalizedDiagnostics,
        };
        operationResults.Publish(result);
        if (showInBrowser)
        {
            this.ApplyOperationResult(result);
        }
    }

    private void ApplyOperationResult(OperationResult result)
    {
        this.OperationResultTitle = result.Title;
        this.OperationResultMessage = BuildOperationMessage(result.Message, result.Diagnostics);
        this.OperationResultSeverity = result.Status switch
        {
            OperationStatus.Succeeded => InfoBarSeverity.Success,
            OperationStatus.SucceededWithWarnings or OperationStatus.PartiallySucceeded => InfoBarSeverity.Warning,
            OperationStatus.Cancelled => InfoBarSeverity.Informational,
            _ => InfoBarSeverity.Error,
        };
        this.IsOperationResultVisible = result.Status is not OperationStatus.Succeeded
                                        || ShouldShowSucceededOperationResult(result.OperationKind);
    }

    private AffectedScope CreateAffectedScope(Uri? scopeUri)
    {
        var project = projectContextService.ActiveProject;
        return new AffectedScope
        {
            ProjectId = project?.ProjectId,
            ProjectName = project?.Name,
            ProjectPath = project?.ProjectRoot,
            AssetId = scopeUri?.ToString(),
            AssetVirtualPath = scopeUri?.AbsolutePath,
        };
    }

    private Uri GetSelectedFolderUri()
    {
        var selected = contentBrowserState.SelectedFolders.FirstOrDefault();
        if (string.IsNullOrWhiteSpace(selected))
        {
            return new Uri("asset:///Content");
        }

        var normalized = selected.Replace('\\', '/').Trim();
        return Uri.TryCreate(normalized, UriKind.Absolute, out var uri)
            && string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            ? uri : new Uri($"{AssetUris.Scheme}:///{normalized.Trim('/')}");
    }

    [RelayCommand]
    private async Task ImportAsync()
    {
        var window = windowManagerService.ActiveWindow?.Window;
        if (window is null)
        {
            return;
        }

        var picker = new FileOpenPicker();
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));
        picker.ViewMode = PickerViewMode.List;
        picker.SuggestedStartLocation = PickerLocationId.DocumentsLibrary;
        picker.FileTypeFilter.Add("*");

        var file = await picker.PickSingleFileAsync();
        if (file is null)
        {
            return;
        }

        var projectRoot = contentBrowserState.ProjectRootPath;
        if (string.IsNullOrEmpty(projectRoot))
        {
            Debug.WriteLine("[AssetsViewModel] Project root path is missing.");
            return;
        }

        var relativePath = this.RetainImportSource(projectRoot, file.Path);
        if (relativePath is not null)
        {
            await this.ImportRetainedSourceAsync(projectRoot, relativePath).ConfigureAwait(true);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The view owns this operation boundary and reports failures.")]
    private string? RetainImportSource(string projectRoot, string sourcePath)
    {
        string relativePath;
        try
        {
            relativePath = Path.GetRelativePath(projectRoot, sourcePath);
        }
        catch
        {
            Debug.WriteLine("[AssetsViewModel] File is not in project directory.");
            return null;
        }

        if (relativePath.StartsWith("..", StringComparison.Ordinal) || Path.IsPathRooted(relativePath))
        {
            var destinationFolder = contentBrowserState.SelectedFolders.FirstOrDefault() ?? "Content";

            destinationFolder = destinationFolder.TrimStart('/', '\\');

            var fileName = Path.GetFileName(sourcePath);
            var destinationPath = Path.Combine(projectRoot, destinationFolder, fileName);

            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);

            try
            {
                File.Copy(sourcePath, destinationPath, overwrite: true);
                Debug.WriteLine($"[AssetsViewModel] Copied {sourcePath} to {destinationPath}");

                relativePath = Path.GetRelativePath(projectRoot, destinationPath);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[AssetsViewModel] Failed to copy file: {ex.Message}");
                return null;
            }
        }

        if (Path.IsPathRooted(relativePath))
        {
            Debug.WriteLine($"[AssetsViewModel] Import failed: relative path '{relativePath}' is still absolute. Check project root and destination paths.");
            return null;
        }

        return relativePath.Replace('\\', '/');
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The view owns this operation boundary and reports failures.")]
    private async Task ImportRetainedSourceAsync(string projectRoot, string relativePath)
    {
        var operationId = Guid.NewGuid();
        var input = new ImportInput(relativePath, this.ResolveImportMountName());
        var request = new ImportRequest(projectRoot, [input], new ImportOptions());

        try
        {
            var result = await importService.ImportAsync(request).ConfigureAwait(true);
            await this.PublishImportResultAsync(operationId, relativePath, result).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[AssetsViewModel] Import exception: {ex}");
            this.PublishFailure(
                ContentPipelineOperationKinds.Import,
                "Import Asset",
                ex.Message,
                AssetImportDiagnosticCodes.ImportFailed,
                this.GetSelectedFolderUri(),
                ex);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The view owns this operation boundary and reports failures.")]
    private async Task PublishImportResultAsync(Guid operationId, string relativePath, ImportResult result)
    {
        if (result.Succeeded)
        {
            Debug.WriteLine($"[AssetsViewModel] Import succeeded for {relativePath}");
            var diagnostics = ToDiagnosticRecords(operationId, result.Diagnostics);
            var status = OperationStatus.Succeeded;
            try
            {
                await assetProvider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(true);
                _ = messenger.Send(new AssetsChangedMessage());
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                status = OperationStatus.PartiallySucceeded;
                diagnostics.Add(this.CreateCatalogRefreshDiagnostic(operationId, ex, this.GetSelectedFolderUri()));
            }

            this.PublishOperation(
                operationId,
                ContentPipelineOperationKinds.Import,
                status,
                "Import Asset",
                $"Imported {relativePath}.",
                diagnostics,
                this.GetSelectedFolderUri());
        }
        else
        {
            Debug.WriteLine($"[AssetsViewModel] Import failed for {relativePath}");
            foreach (var diag in result.Diagnostics)
            {
                Debug.WriteLine($"[Import] {diag.Severity}: {diag.Message}");
            }

            this.PublishOperation(
                operationId,
                ContentPipelineOperationKinds.Import,
                OperationStatus.Failed,
                "Import Asset",
                $"Import failed for {relativePath}.",
                ToDiagnosticRecords(operationId, result.Diagnostics),
                this.GetSelectedFolderUri());
        }
    }

    private string ResolveImportMountName()
    {
        var folderUri = this.GetSelectedFolderUri();
        var path = Uri.UnescapeDataString(folderUri.AbsolutePath).Trim('/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        var mountName = slash < 0 ? path : path[..slash];
        var mounts = projectContextService.ActiveProject?.AuthoringMounts;
        return !string.IsNullOrWhiteSpace(mountName) ? mountName
            : mounts is { Count: > 0 } ? mounts[0].Name : "Content";
    }

    private DiagnosticRecord CreateCatalogRefreshDiagnostic(
        Guid operationId,
        Exception exception,
        Uri? scopeUri)
        => new()
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetIdentity,
            Severity = DiagnosticSeverity.Error,
            Code = AssetIdentityDiagnosticCodes.RefreshFailed,
            Message = "Asset catalog refresh failed after import.",
            TechnicalMessage = exception.Message,
            ExceptionType = exception.GetType().FullName,
            AffectedEntity = this.CreateAffectedScope(scopeUri),
        };
}
