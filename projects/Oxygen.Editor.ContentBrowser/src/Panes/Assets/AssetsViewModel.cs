// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DroidNet.Storage;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Importing;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.Textures;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
///     The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
/// <param name="automaticCooking">Cooks changed sources and holds the project's Cook on change setting.</param>
/// <param name="vmToViewConverter">The converter for converting view models to views.</param>
/// <param name="contentBrowserState">The content browser state to track selection changes.</param>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="contentPipelineService">The explicit editor content-pipeline service.</param>
/// <param name="assetProvider">The shared content-browser asset provider.</param>
/// <param name="operationResults">The operation-result publisher.</param>
/// <param name="statusReducer">The operation status reducer.</param>
/// <param name="storage">The storage provider.</param>
/// <param name="dialogService">The existing dialog and folder-picker service.</param>
/// <param name="windowManagerService">The window manager service.</param>
/// <param name="shell">The clipboard and File Explorer actions.</param>
/// <param name="textureImport">Imports images as textures.</param>
/// <param name="relocation">The rename, move, copy and delete workflow; without it those commands are unavailable.</param>
/// <param name="creation">New Script, New Input File and New Scene; without it those commands are unavailable.</param>
/// <param name="codeEditor">The code editor that opens scripts and input files; without it Edit is unavailable.</param>
/// <param name="placement">Opens scenes and materials and shows panels; without it opening them does nothing.</param>
/// <param name="documents">Saves the unsaved documents a cook reads, after asking; without it a cook does not ask.</param>
/// <param name="unsavedInputs">Finds the unsaved documents a cook reads; without it a cook does not ask.</param>
public partial class AssetsViewModel(
    Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService automaticCooking,
    ViewModelToView vmToViewConverter,
    ContentBrowserState contentBrowserState,
    IProjectContextService projectContextService,
    IContentPipelineService contentPipelineService,
    IAssetFeed assetProvider,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    IStorageProvider storage,
    IMessenger messenger,
    IDialogService dialogService,
    IWindowManagerService windowManagerService,
    IDesktopIntegration shell,
    TextureImport textureImport,
    Relocation.IAssetRelocationWorkflow? relocation = null,
    Authoring.AuthoredAssetCreation? creation = null,
    ICodeEditorService? codeEditor = null,
    IPlacementService? placement = null,
    IDocumentService? documents = null,
    IUnsavedCookInputs? unsavedInputs = null) : AbstractOutletContainer, IRoutingAware
{
    private bool disposed;

    private bool isInitialized;

    /// <summary>Gets a value indicating whether the project cooks changed sources without being asked.</summary>
    public bool CookOnChange => automaticCooking.CookOnChange;

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

            messenger.Register<CreateScriptRequestMessage>(this, (_, message) => message.Reply(creation?.CreateScriptAsync() ?? Task.FromResult<FileSystemPath?>(null)));
            relocation?.Completed += this.OnRelocationCompleted;
            relocation?.HistoryChanged += this.OnRelocationHistoryChanged;
            automaticCooking.PropertyChanged += this.OnAutomaticCookingChanged;

            // Indexing is started by ContentBrowserViewModel - no need to start here
            this.isInitialized = true;
        }

        // Asset indexing runs automatically in background with file watching
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

    /// <summary>Reviews and imports a source selected by the picker or the browser.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="sourcePath">The selected primary source.</param>
    /// <returns>The review and visible import operation.</returns>
    internal async Task ImportSourceFileAsync(ProjectContext project, string sourcePath)
    {
        try
        {
            if (!ReferenceEquals(projectContextService.ActiveProject, project))
            {
                await dialogService.ShowMessageAsync("Import model", "The project changed. Select the source again in the current project.").ConfigureAwait(true);
                return;
            }

            var selected = this.GetSelectedFolder();
            var mount = project.AuthoringMounts.FirstOrDefault(mount => string.Equals(selected.Mount, mount.Name, StringComparison.Ordinal))
                ?? project.AuthoringMounts.FirstOrDefault(static mount => string.Equals(mount.Name, "Content", StringComparison.Ordinal))
                ?? project.AuthoringMounts[0];
            var destination = "/" + mount.Name;

            var model = new SceneImportDialogViewModel(project, sourcePath, destination, dialogService);
            var view = this.VmToViewConverter.Convert(model, typeof(object), parameter: null, language: CultureInfo.CurrentUICulture.Name)
                ?? throw new InvalidOperationException("The import review view is unavailable.");
            var button = await dialogService.ShowAsync(new DialogSpec("Import model", view)
            {
                PrimaryButtonText = "Import",
                CloseButtonText = "Cancel",
                DefaultButton = DialogButton.Primary,
                PrimaryAction = () => Task.FromResult(model.Validate()),
            }).ConfigureAwait(true);
            if (button == DialogButton.Primary && model.Request is { } request)
            {
                await this.RunSourceImportAsync(project, () => contentPipelineService.ImportSourceAsync(request, CancellationToken.None)).ConfigureAwait(true);
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or System.Runtime.InteropServices.COMException)
        {
            await dialogService.ShowMessageAsync("Import model", error.Message).ConfigureAwait(true);
        }
    }

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
                automaticCooking.PropertyChanged -= this.OnAutomaticCookingChanged;

                // Cleanup event subscriptions
                contentBrowserState.PropertyChanged -= this.OnContentBrowserStatePropertyChanged;
                relocation?.Completed -= this.OnRelocationCompleted;
                relocation?.HistoryChanged -= this.OnRelocationHistoryChanged;

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

    private static List<DiagnosticRecord> NormalizeDiagnostics(
        Guid operationId,
        IEnumerable<DiagnosticRecord> diagnostics)
        => [.. diagnostics
            .Select(diagnostic => diagnostic.OperationId == operationId
                ? diagnostic
                : diagnostic with { OperationId = operationId })];

    private static string DescribeScope(AssetPath? scopeUri)
        => scopeUri?.ToString() ?? "the active project";

    private async Task ImportImageSourceAsync(ProjectContext project, string sourcePath)
    {
        if (!ReferenceEquals(projectContextService.ActiveProject, project))
        {
            await dialogService.ShowMessageAsync("Import texture", "The project changed. Select the image again in the current project.").ConfigureAwait(true);
            return;
        }

        var destination = this.GetSelectedTextureFolder(project);
        var model = new TextureImportDialogViewModel(project, sourcePath, destination, textureImport, dialogService);
        var view = new TextureImportDialogView(model);
        var button = await dialogService.ShowAsync(new DialogSpec("Import texture", view)
        {
            PrimaryButtonText = "Import",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
            PrimaryAction = () => Task.FromResult(model.Validate()),
        }).ConfigureAwait(true);
        if (button != DialogButton.Primary || model.Request is not { } request)
        {
            return;
        }

        this.IsOperationResultVisible = false;
        try
        {
            var textureUri = await textureImport.CreateAsync(request).ConfigureAwait(true);
            await this.RunSourceImportAsync(
                project,
                () => contentPipelineService.CookAssetAsync(textureUri, CancellationToken.None, project),
                "Import texture",
                textureUri).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            // The active texture import is cancelled before publication.
        }
        catch (TextureImportException error)
        {
            this.PublishFailure(ContentPipelineOperationKinds.Import, "Import texture", error.Message, error.Code, request.DestinationFolder, error, showInBrowser: false);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException)
        {
            this.PublishFailure(ContentPipelineOperationKinds.Import, "Import texture", error.Message, AssetImportDiagnosticCodes.ImportFailed, request.DestinationFolder, error, showInBrowser: false);
        }
    }

    private string GetSelectedTextureFolder(ProjectContext project)
    {
        var selected = this.GetSelectedFolder();
        var parts = selected.VirtualPath.Trim('/').Split('/');
        var mountName = parts.Length > 0
            ? project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, parts[0], StringComparison.Ordinal))?.Name
            : null;
        mountName ??= project.AuthoringMounts.FirstOrDefault(static mount => string.Equals(mount.Name, "Content", StringComparison.Ordinal))?.Name
            ?? (project.AuthoringMounts.Count > 0
                ? project.AuthoringMounts[0].Name
                : throw new InvalidOperationException("The project has no authoring mount for texture assets."));
        if (parts.Skip(1).Any(static part => string.Equals(part, "Textures", StringComparison.OrdinalIgnoreCase)))
        {
            return "/" + string.Join('/', parts);
        }

        return "/" + mountName + "/Textures";
    }

    private void OnContentBrowserStatePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (!this.disposed && e.PropertyName is nameof(ContentBrowserState.SelectedFolders) or nameof(ContentBrowserState.SelectedAssets))
        {
            this.NotifyCookSelection();
        }

        // The sources tree also cuts and copies folders onto the shared clipboard.
        if (!this.disposed && e.PropertyName is nameof(ContentBrowserState.Clipboard))
        {
            this.NotifyAssetActions();
        }
    }

    private async void OnAssetItemInvoked(object? sender, AssetsViewItemInvokedEventArgs args)
    {
        _ = sender; // Unused

        Debug.WriteLine(
            $"[AssetsViewModel] Item invoked: {args.InvokedItem.DisplayName}, Kind: {args.InvokedItem.Kind}, URI: {args.InvokedItem.Identity}");

        if (await this.TryEditInvokedAsync(args.InvokedItem).ConfigureAwait(true))
        {
            return;
        }

        if (args.InvokedItem.Kind != AssetKind.Folder
            && (args.InvokedItem.IsPackaged || args.InvokedItem.IsCookedOnly))
        {
            await this.OpenInspectionAsync(validate: false, args.InvokedItem.Identity, args.InvokedItem).ConfigureAwait(true);
        }
        else if (args.InvokedItem.Kind == AssetKind.Scene)
        {
            // The scene's identity is its file; the Shell opens it in place of the window's scene.
            await this.OpenDocumentAsync(args.InvokedItem.Identity).ConfigureAwait(true);
        }
        else if (args.InvokedItem.Kind == AssetKind.Folder)
        {
            // Navigate into the folder
            Debug.WriteLine($"[AssetsViewModel] Navigating to folder: {args.InvokedItem.DisplayPath}");
            await this.NavigateToFolder(args.InvokedItem.DisplayPath).ConfigureAwait(false);
        }
        else if (args.InvokedItem is { Kind: AssetKind.ForeignSource, SourcePath: { } sourcePath } && projectContextService.ActiveProject is { } project)
        {
            if (project.FileSystem.File.Exists(sourcePath.FullPath + NativeSceneImportSettings.SidecarSuffix))
            {
                await this.RunSourceImportAsync(project, () => contentPipelineService.ReimportSourceAsync(args.InvokedItem.Identity, project, CancellationToken.None)).ConfigureAwait(true);
            }
            else
            {
                await this.ImportSourceFileAsync(project, sourcePath.FullPath).ConfigureAwait(true);
            }
        }
        else if (args.InvokedItem is { Kind: AssetKind.Material, DescriptorPath: not null })
        {
            await this.OpenDocumentAsync(args.InvokedItem.Identity).ConfigureAwait(true);
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
    ///     Asks for a name and folder, creates a new material there and opens it.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    [RelayCommand]
    private async Task CreateNewMaterialWithPromptAsync()
    {
        if (creation is not null)
        {
            _ = await creation.CreateMaterialAsync().ConfigureAwait(true);
        }
    }

    /// <summary>
    ///     Asks for a name and folder, creates an empty scene there and opens it.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    [RelayCommand]
    private async Task CreateNewSceneWithPromptAsync()
    {
        if (creation is not null)
        {
            _ = await creation.CreateSceneAsync().ConfigureAwait(true);
        }
    }

    /// <summary>Turns the project's Cook on change setting on or off.</summary>
    /// <returns>Completion after the setting is saved, or its failure is reported.</returns>
    [RelayCommand]
    private async Task ToggleCookOnChangeAsync()
    {
        var enabled = !automaticCooking.CookOnChange;
        try
        {
            await automaticCooking.SetCookOnChangeAsync(enabled).ConfigureAwait(true);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidOperationException or System.Text.Json.JsonException)
        {
            this.PublishFailure(
                ContentPipelineOperationKinds.CookOnChangeSet,
                "Cook on change",
                "The project's Cook on change setting could not be saved: " + error.Message,
                ContentPipelineDiagnosticCodes.CookOnChangeNotSaved,
                scopeUri: null,
                error,
                showInBrowser: false);

            // The menu item flipped itself; show the setting as it is.
            this.OnPropertyChanged(nameof(this.CookOnChange));
        }
    }

    private void OnAutomaticCookingChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService.CookOnChange), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.CookOnChange));
        }
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

    // A batch cooks only when every selected asset has a saved authored recipe.
    private bool CanCookSelectedAsset()
        => !this.disposed && this.isInitialized && this.SelectedAssets.Count > 0 && this.SelectedAssets.All(static asset => asset.CanCook);

    private bool CanCookSelectedFolder() => this.IsSelectedFolderWritable();

    private bool CanCreateFolder() => this.IsSelectedFolderWritable();

    private bool IsSelectedFolderWritable()
    {
        if (this.disposed || !this.isInitialized || projectContextService.ActiveProject is not { } project)
        {
            return false;
        }

        var folder = this.GetSelectedFolder();
        return project.AuthoringMounts.Any(mount => !ProjectExplorer.ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(mount)
            && string.Equals(folder.Mount, mount.Name, StringComparison.Ordinal));
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
        this.ReimportSelectedSourceCommand.NotifyCanExecuteChanged();
        this.ShowImportSourceCommand.NotifyCanExecuteChanged();
        this.CookSelectedAssetCommand.NotifyCanExecuteChanged();
        this.CookSelectedFolderCommand.NotifyCanExecuteChanged();
        this.CreateFolderCommand.NotifyCanExecuteChanged();
        this.NotifyAssetActions();
    }

    [RelayCommand(CanExecute = nameof(CanCreateFolder))]
    private void CreateFolder()
    {
        if (this.CanCreateFolder())
        {
            _ = messenger.Send(new CreateFolderRequestMessage(this.GetSelectedFolder()));
        }
    }

    [RelayCommand(CanExecute = nameof(CanCookSelectedAsset))]
    private async Task CookSelectedAssetAsync()
    {
        var assets = this.SelectedAssets.Where(static asset => asset.Kind != AssetKind.Folder).ToArray();
        if (assets.Length == 0)
        {
            this.PublishFailure(
                ContentPipelineOperationKinds.CookAsset,
                "No asset selected",
                "Select one or more cookable assets before running Cook Selected.",
                AssetCookDiagnosticCodes.CookFailed,
                scopeUri: null);
            return;
        }

        if (assets.FirstOrDefault(static asset => !asset.CanCook) is { } ineligible)
        {
            this.PublishFailure(
                ContentPipelineOperationKinds.CookAsset,
                "Cook Asset",
                $"{ineligible.DisplayName} has no retained source content. Select only assets with saved authored sources.",
                AssetCookDiagnosticCodes.CookFailed,
                ineligible.Identity);
            return;
        }

        if (!await this.SaveInputsAsync(new CookRunRequest(CookTargetKind.Changes, ScopeUri: null) { Assets = [.. assets.Select(static asset => asset.Identity)] }).ConfigureAwait(true))
        {
            return;
        }

        foreach (var asset in assets)
        {
            await this.RunCookAsync(
                    ContentPipelineOperationKinds.CookAsset,
                    "Cook Asset",
                    asset.Identity,
                    () => contentPipelineService.CookAssetAsync(asset.Identity, CancellationToken.None))
                .ConfigureAwait(true);
        }
    }

    [RelayCommand(CanExecute = nameof(CanCookSelectedFolder))]
    private async Task CookSelectedFolderAsync()
    {
        var folder = this.GetSelectedFolder();
        if (await this.SaveInputsAsync(new CookRunRequest(CookTargetKind.Folder, folder)).ConfigureAwait(true))
        {
            await this.RunCookAsync(
                    ContentPipelineOperationKinds.CookFolder,
                    "Cook Folder",
                    folder,
                    () => contentPipelineService.CookFolderAsync(folder, CancellationToken.None))
                .ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task CookProjectAsync()
    {
        if (await this.SaveInputsAsync(new CookRunRequest(CookTargetKind.Project, ScopeUri: null)).ConfigureAwait(true))
        {
            await this.RunCookAsync(
                    ContentPipelineOperationKinds.CookProject,
                    "Cook Project",
                    scopeUri: null,
                    () => contentPipelineService.CookProjectAsync(CancellationToken.None))
                .ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private Task InspectCookedOutputAsync() => this.OpenSelectedInspectionAsync(validate: false);

    [RelayCommand]
    private Task ValidateCookedOutputAsync() => this.OpenSelectedInspectionAsync(validate: true);

    private Task OpenSelectedInspectionAsync(bool validate)
        => this.isInitialized && this.LayoutViewModel is AssetsLayoutViewModel { SelectedAsset: { Kind: not AssetKind.Folder } asset }
            ? this.OpenInspectionAsync(validate, asset.Identity, asset) : this.OpenInspectionAsync(validate);

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI reports a document-opening failure at its operation boundary.")]
    private async Task OpenInspectionAsync(bool validate, AssetPath? identity = null, AssetEntry? asset = null)
    {
        // A library's copy is inspected where the library shows it.
        var scope = asset?.CookedMetadata is not null && AssetPath.TryParse(asset.DisplayPath, out var shown) ? shown : identity ?? this.GetSelectedFolder();
        try
        {
            if (projectContextService.ActiveProject is not { } project)
            {
                return;
            }

            var request = messenger.Send(new OpenCookedInspectionRequestMessage(project, scope, validate)
            {
                Asset = identity,
                CookedSource = asset?.CookedMetadata,
                DisplayName = asset?.DisplayName,
            });
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
                scope,
                ex);
        }
    }

    // A cook reads the saved files: the unsaved documents it reads are saved first, after asking.
    private Task<bool> SaveInputsAsync(CookRunRequest request)
        => documents is null || unsavedInputs is null
            ? Task.FromResult(true)
            : documents.SaveBeforeAsync(SaveBeforeOperation.Cook, token => unsavedInputs.FindAsync(request, token));

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The view owns this operation boundary and reports failures.")]
    private async Task RunCookAsync(
        string operationKind,
        string title,
        AssetPath? scopeUri,
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
        AssetPath? scopeUri)
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
        AssetPath? scopeUri,
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
        AssetPath? scopeUri,
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

    private AffectedScope CreateAffectedScope(AssetPath? scopeUri)
    {
        var project = projectContextService.ActiveProject;
        return new AffectedScope
        {
            ProjectId = project?.ProjectId,
            ProjectName = project?.Name,
            ProjectPath = project?.ProjectRoot.FullPath,
            AssetId = scopeUri?.ToUriString(),
            AssetVirtualPath = scopeUri?.VirtualPath,
        };
    }

    // The folder that folder-wide commands act on: the first selected one, or the Content mount when none is.
    private AssetPath GetSelectedFolder()
        => contentBrowserState.SelectedFolders.Order().FirstOrDefault() ?? AssetPath.Content;

    [RelayCommand]
    private Task ImportAsync() => this.ImportPickedSourceAsync(models: true, textures: true);

    [RelayCommand]
    private Task ImportModelAsync() => this.ImportPickedSourceAsync(models: true, textures: false);

    [RelayCommand]
    private Task ImportTextureAsync() => this.ImportPickedSourceAsync(models: false, textures: true);

    private async Task ImportPickedSourceAsync(bool models, bool textures)
    {
        try
        {
            var project = projectContextService.ActiveProject;
            var window = windowManagerService.ActiveWindow?.Window;
            if (project is null || window is null)
            {
                return;
            }

            var modelTypes = AssetFileTypes.Suffixes(AssetFileCategory.Model).ToList();
            var textureTypes = TextureImport.SupportedExtensions.ToList();
            var filters = new Dictionary<string, IList<string>>(StringComparer.Ordinal);
            if (models && textures)
            {
                filters["Supported assets"] = [.. modelTypes.Concat(textureTypes).Distinct(StringComparer.OrdinalIgnoreCase)];
            }

            if (models)
            {
                filters["3D models"] = modelTypes;
            }

            if (textures)
            {
                filters["Textures"] = textureTypes;
            }

            var picker = new FilePickerSpec(
                models && textures ? "Import asset" : models ? "Import source model" : "Import texture or image",
                "Oxygen.ImportAsset",
                filters)
            {
                SuggestedStartFolder = project.ProjectRoot.FullPath,
            };

            if (await dialogService.PickFileAsync(picker, window.AppWindow.Id).ConfigureAwait(true) is { } path)
            {
                if (TextureImport.IsSupportedImage(path))
                {
                    await this.ImportImageSourceAsync(project, path).ConfigureAwait(true);
                }
                else
                {
                    await this.ImportSourceFileAsync(project, path).ConfigureAwait(true);
                }
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            await dialogService.ShowMessageAsync("Import", error.Message).ConfigureAwait(true);
        }
    }

    private bool CanReimportSelectedSource() => !this.disposed && this.isInitialized
        && this.SelectedAssets.Count <= 1 && this.LayoutViewModel is AssetsLayoutViewModel { SelectedAsset.CanReimport: true };

    private bool CanShowImportSource() => !this.disposed && this.isInitialized && this.SelectedAssets.Count <= 1
        && this.LayoutViewModel is AssetsLayoutViewModel { SelectedAsset: { ImportSource: { } source } asset } && source != asset.Identity;

    [RelayCommand(CanExecute = nameof(CanShowImportSource))]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Source navigation reports failures at its user-command boundary.")]
    private async Task ShowImportSourceAsync()
    {
        try
        {
            if (projectContextService.ActiveProject is { } project && this.LayoutViewModel is AssetsLayoutViewModel { SelectedAsset.ImportSource: { } source })
            {
                var request = messenger.Send(new ShowAssetRequestMessage(project, source));
                if (!request.HasReceivedResponse || !await request.Response.ConfigureAwait(true))
                {
                    await dialogService.ShowMessageAsync("Show source", "The retained model source is no longer available in Content Browser.").ConfigureAwait(true);
                }
            }
        }
        catch (OperationCanceledException)
        {
            // Superseded navigation leaves the current project selection intact.
        }
        catch (Exception error)
        {
            await dialogService.ShowMessageAsync("Show source", error.Message).ConfigureAwait(true);
        }
    }

    [RelayCommand(CanExecute = nameof(CanReimportSelectedSource))]
    private async Task ReimportSelectedSourceAsync()
    {
        if (projectContextService.ActiveProject is { } project && this.LayoutViewModel is AssetsLayoutViewModel { SelectedAsset.ImportSource: { } source })
        {
            await this.RunSourceImportAsync(project, () => contentPipelineService.ReimportSourceAsync(source, project, CancellationToken.None)).ConfigureAwait(true);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "This user-command boundary reports failures; the coordinator owns native work and scoped recovery.")]
    private async Task RunSourceImportAsync(ProjectContext project, Func<Task<ContentCookResult>> import, string operationTitle = "Import model", AssetPath? operationScope = null)
    {
        this.IsOperationResultVisible = false;
        try
        {
            var result = await import().ConfigureAwait(true);
            if (ReferenceEquals(projectContextService.ActiveProject, project))
            {
                await assetProvider.RefreshAsync().ConfigureAwait(true);
                this.NotifyCookSelection();
            }

            this.PublishCookResult(ContentPipelineOperationKinds.Import, operationTitle, result, operationScope ?? result.RetainedSourceUri);
        }
        catch (OperationCanceledException)
        {
            // The Cooking run retains the cancellation outcome.
        }
        catch (Exception error)
        {
            this.PublishFailure(ContentPipelineOperationKinds.Import, operationTitle, error.Message, AssetImportDiagnosticCodes.ImportFailed, operationScope, error, showInBrowser: false);
        }
    }
}
