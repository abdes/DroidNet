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
using Microsoft.UI.Xaml.Controls;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
///     The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
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

    /// <summary>
    ///     Gets the converter for converting view models to views.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; } = vmToViewConverter;

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

            // If no browser state was restored, default to the scene assets folder.
            // Workspace activation owns initial scene opening/restoration.
            var activeContext = projectContextService.ActiveProject;
            if (activeContext?.Scenes.Count > 0 && contentBrowserState.SelectedFolders.Count == 0)
            {
                // Navigate to the Scenes folder to show scene assets
                contentBrowserState.SetSelectedFolders(["Content/Scenes"]);
            }
        }

        // Asset indexing runs automatically in background with file watching
    }

    /// <summary>
    ///     Releases the unmanaged resources used by the <see cref="AssetsViewModel" /> and optionally releases the managed
    ///     resources.
    /// </summary>
    /// <param name="disposing">
    ///     true to release both managed and unmanaged resources; false to release only unmanaged
    ///     resources.
    /// </param>
    protected new virtual void Dispose(bool disposing)
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
                if (this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
                {
                    layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
                }
            }

            this.disposed = true;
        }
    }

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
            if (!MaterialSourceExists(folder, candidate))
            {
                return candidate;
            }
        }

        return string.Create(CultureInfo.InvariantCulture, $"NewMaterial{Guid.NewGuid():N}");
    }

    public string GetSelectedMaterialFolder()
        => this.ResolveMaterialFolder(contentBrowserState.SelectedFolders.FirstOrDefault());

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

    private void OnLayoutViewModelChanging(object? sender, PropertyChangingEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
        }
    }

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
            || normalized == "."
            || normalized == ".."
            || normalized.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            normalized = string.Empty;
            return false;
        }

        return true;
    }

    private void OnLayoutViewModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked += this.OnAssetItemInvoked;
        }
    }

    [RelayCommand]
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
                null);
            return;
        }

        if (!IsCookableDescriptorSelection(asset))
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

    [RelayCommand]
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
    private async Task InspectCookedOutputAsync()
    {
        var scopeUri = this.GetSelectedFolderUri();
        var operationId = Guid.NewGuid();
        try
        {
            var result = await contentPipelineService.InspectCookedOutputAsync(scopeUri, CancellationToken.None)
                .ConfigureAwait(true);
            this.PublishOperation(
                operationId,
                ContentPipelineOperationKinds.CookedOutputInspect,
                result.Succeeded ? OperationStatus.Succeeded : OperationStatus.Failed,
                "Inspect Cooked Output",
                result.Succeeded
                    ? $"Found {result.Assets.Count} cooked assets and {result.Files.Count} cooked files in {result.CookedRoot}."
                    : $"Cooked output inspection failed: {result.CookedRoot}.",
                result.Diagnostics,
                scopeUri);
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

    [RelayCommand]
    private async Task ValidateCookedOutputAsync()
    {
        var scopeUri = this.GetSelectedFolderUri();
        try
        {
            var result = await contentPipelineService.ValidateCookedOutputAsync(scopeUri, CancellationToken.None)
                .ConfigureAwait(true);
            this.PublishOperation(
                Guid.NewGuid(),
                ContentPipelineOperationKinds.CookedOutputValidate,
                result.Succeeded ? OperationStatus.Succeeded : OperationStatus.Failed,
                "Validate Cooked Output",
                result.Succeeded
                    ? $"Cooked output validated: {result.CookedRoot}."
                    : $"Cooked output validation failed: {result.CookedRoot}.",
                result.Diagnostics,
                scopeUri);
            if (result.Succeeded)
            {
                _ = messenger.Send(new ValidatedCookedOutputMessage([result.CookedRoot]));
            }
        }
        catch (Exception ex)
        {
            this.PublishFailure(
                ContentPipelineOperationKinds.CookedOutputValidate,
                "Validate Cooked Output",
                ex.Message,
                ContentPipelineDiagnosticCodes.ValidateFailed,
                scopeUri,
                ex);
        }
    }

    private async Task RunCookAsync(
        string operationKind,
        string title,
        Uri? scopeUri,
        Func<Task<ContentCookResult>> cook)
    {
        try
        {
            var result = await this.RefreshCatalogAfterCookAsync(await cook().ConfigureAwait(true), scopeUri)
                .ConfigureAwait(true);
            this.PublishCookResult(operationKind, title, result, scopeUri);
            if (result.Validation?.Succeeded == true && result.Status is not OperationStatus.Failed)
            {
                _ = messenger.Send(new ValidatedCookedOutputMessage(GetValidatedCookedRoots(result)));
            }
        }
        catch (Exception ex)
        {
            this.PublishFailure(
                operationKind,
                title,
                ex.Message,
                AssetCookDiagnosticCodes.CookFailed,
                scopeUri,
                ex);
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
            scopeUri);
    }

    private void PublishFailure(
        string operationKind,
        string title,
        string message,
        string code,
        Uri? scopeUri,
        Exception? exception = null)
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
        this.PublishOperation(operationId, operationKind, OperationStatus.Failed, title, message, [diagnostic], scopeUri);
    }

    private void PublishOperation(
        Guid operationId,
        string operationKind,
        OperationStatus status,
        string title,
        string message,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        Uri? scopeUri)
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
        this.ApplyOperationResult(result);
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

    internal static string BuildOperationMessage(string message, IReadOnlyList<DiagnosticRecord> diagnostics)
    {
        var diagnostic = diagnostics.FirstOrDefault(static item =>
            item.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal)
                         ?? diagnostics.FirstOrDefault(static item => item.Severity == DiagnosticSeverity.Warning);
        if (diagnostic is null)
        {
            return message;
        }

        var details = string.IsNullOrWhiteSpace(diagnostic.TechnicalMessage)
            ? diagnostic.Message
            : diagnostic.TechnicalMessage;
        return string.IsNullOrWhiteSpace(details)
            || string.Equals(details, message, StringComparison.Ordinal)
            || message.Contains(details, StringComparison.Ordinal)
            ? message
            : $"{message} {details}";
    }

    internal static bool ShouldShowSucceededOperationResult(string operationKind)
        => string.Equals(operationKind, ContentPipelineOperationKinds.CookedOutputInspect, StringComparison.Ordinal)
           || string.Equals(operationKind, ContentPipelineOperationKinds.CookedOutputValidate, StringComparison.Ordinal);

    private async Task<ContentCookResult> RefreshCatalogAfterCookAsync(ContentCookResult result, Uri? scopeUri)
    {
        if (result.Status == OperationStatus.Failed)
        {
            return result;
        }

        try
        {
            await assetProvider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(true);
            _ = messenger.Send(new AssetsChangedMessage(scopeUri));
            return result;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            var diagnostic = new DiagnosticRecord
            {
                OperationId = result.OperationId,
                Domain = FailureDomain.AssetIdentity,
                Severity = DiagnosticSeverity.Error,
                Code = AssetIdentityDiagnosticCodes.RefreshFailed,
                Message = "Asset catalog refresh failed after cook.",
                TechnicalMessage = ex.Message,
                ExceptionType = ex.GetType().FullName,
                AffectedEntity = this.CreateAffectedScope(scopeUri),
            };
            return result with
            {
                Status = result.Status == OperationStatus.Succeeded
                    ? OperationStatus.PartiallySucceeded
                    : result.Status,
                Diagnostics = [.. result.Diagnostics, diagnostic],
            };
        }
    }

    private static IReadOnlyList<string> GetValidatedCookedRoots(ContentCookResult result)
        => result.Validation?.CookedRoot
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Where(static root => !string.IsNullOrWhiteSpace(root))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList()
           ?? [];

    private static IReadOnlyList<DiagnosticRecord> NormalizeDiagnostics(
        Guid operationId,
        IEnumerable<DiagnosticRecord> diagnostics)
        => diagnostics
            .Select(diagnostic => diagnostic.OperationId == operationId
                ? diagnostic
                : diagnostic with { OperationId = operationId })
            .ToList();

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
        if (Uri.TryCreate(normalized, UriKind.Absolute, out var uri)
            && string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return uri;
        }

        return new Uri($"{AssetUris.Scheme}:///{normalized.Trim('/')}");
    }

    private static string DescribeScope(Uri? scopeUri)
        => scopeUri?.ToString() ?? "the active project";

    private static bool IsCookableDescriptorSelection(ContentBrowserAssetItem asset)
        => asset.IdentityUri.AbsolutePath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
           || asset.IdentityUri.AbsolutePath.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
           || asset.IdentityUri.AbsolutePath.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase);

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

        string relativePath;
        try
        {
            relativePath = Path.GetRelativePath(projectRoot, file.Path);
        }
        catch
        {
            Debug.WriteLine("[AssetsViewModel] File is not in project directory.");
            return;
        }

        if (relativePath.StartsWith("..", StringComparison.Ordinal) || Path.IsPathRooted(relativePath))
        {
            // File is outside project directory. Copy it to the currently selected folder.
            var destinationFolder = contentBrowserState.SelectedFolders.FirstOrDefault() ?? "Content";

            // Ensure destinationFolder is treated as relative to projectRoot by trimming leading slashes.
            // Otherwise, Path.Combine might treat it as an absolute path on the current drive.
            destinationFolder = destinationFolder.TrimStart('/', '\\');

            var fileName = Path.GetFileName(file.Path);
            var destinationPath = Path.Combine(projectRoot, destinationFolder, fileName);

            // Ensure destination directory exists
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);

            try
            {
                File.Copy(file.Path, destinationPath, overwrite: true);
                Debug.WriteLine($"[AssetsViewModel] Copied {file.Path} to {destinationPath}");

                // Update relative path to point to the copied file
                relativePath = Path.GetRelativePath(projectRoot, destinationPath);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[AssetsViewModel] Failed to copy file: {ex.Message}");
                return;
            }
        }

        if (Path.IsPathRooted(relativePath))
        {
            Debug.WriteLine($"[AssetsViewModel] Import failed: relative path '{relativePath}' is still absolute. Check project root and destination paths.");
            return;
        }

        relativePath = relativePath.Replace('\\', '/');

        var operationId = Guid.NewGuid();
        var input = new ImportInput(relativePath, this.ResolveImportMountName());
        var request = new ImportRequest(projectRoot, [input], new ImportOptions());

        try
        {
            var result = await importService.ImportAsync(request);
            if (result.Succeeded)
            {
                Debug.WriteLine($"[AssetsViewModel] Import succeeded for {relativePath}");
                var diagnostics = ToDiagnosticRecords(operationId, result.Diagnostics).ToList();
                var status = OperationStatus.Succeeded;
                try
                {
                    await assetProvider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(true);
                    _ = messenger.Send(new AssetsChangedMessage());
                }
                catch (Exception ex) when (ex is not OperationCanceledException)
                {
                    status = OperationStatus.PartiallySucceeded;
                    diagnostics.Add(CreateCatalogRefreshDiagnostic(operationId, ex, this.GetSelectedFolderUri()));
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

    private string ResolveImportMountName()
    {
        var folderUri = this.GetSelectedFolderUri();
        var path = Uri.UnescapeDataString(folderUri.AbsolutePath).Trim('/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        var mountName = slash < 0 ? path : path[..slash];
        if (!string.IsNullOrWhiteSpace(mountName))
        {
            return mountName;
        }

        return projectContextService.ActiveProject?.AuthoringMounts.FirstOrDefault()?.Name ?? "Content";
    }

    private static IReadOnlyList<DiagnosticRecord> ToDiagnosticRecords(
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
