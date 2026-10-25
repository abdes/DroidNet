// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using LooseCookedFileKinds = Oxygen.Managed.Assets.Persistence.LooseCooked.FileKindExtensions;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Default explicit editor content-pipeline workflow service.
/// </summary>
/// <param name="session">The project session the pipeline cooks for; its lifetime cancels the session's reads.</param>
/// <param name="cookCoordinator">The shared project writer and lifetime coordinator.</param>
/// <param name="cookScopeProvider">The project cook scope provider.</param>
/// <param name="sceneDescriptorGenerator">The scene descriptor generator.</param>
/// <param name="manifestBuilder">The import manifest builder.</param>
/// <param name="manifestValidator">The import manifest validator.</param>
/// <param name="engineContentPipelineApi">The engine content-pipeline adapter.</param>
/// <param name="nativeCompatibility">The compatible producer identity and file ownership.</param>
/// <param name="files">Atomic storage for model import records.</param>
/// <param name="publication">The shared journal and runtime-publication owner.</param>
public sealed partial class ContentPipelineService(
    IProjectSession session,
    IContentCookCoordinator cookCoordinator,
    IProjectCookScopeProvider cookScopeProvider,
    ISceneDescriptorGenerator sceneDescriptorGenerator,
    IContentImportManifestBuilder manifestBuilder,
    IContentImportManifestValidator manifestValidator,
    IEngineContentPipelineApi engineContentPipelineApi,
    INativeCompatibilityService nativeCompatibility,
    DroidNet.Storage.IAtomicFileStore files,
    Publication.CookPublicationService publication,
    IEngineContentPak engineContent) : IContentPipelineService
{
    /// <summary>The code of the Info diagnostic that names a moved or deleted source whose cooked output a cook removes.</summary>
    internal const string OutputRetiredCode = "asset_cook.output_retired";

    /// <summary>The code of the Info diagnostic that names a changed source cook on change leaves out for its errors.</summary>
    internal const string SkippedForProblemsCode = "asset_cook.skipped_for_problems";

    private readonly INativeCompatibilityService nativeCompatibility = nativeCompatibility;
    private readonly Publication.CookPublicationService publication = publication ?? throw new ArgumentNullException(nameof(publication));
    private readonly IEngineContentPak engineContent = engineContent ?? throw new ArgumentNullException(nameof(engineContent));

    private readonly IProjectSession session = session ?? throw new ArgumentNullException(nameof(session));
    private readonly IContentCookCoordinator cookCoordinator = cookCoordinator ?? throw new ArgumentNullException(nameof(cookCoordinator));
    private readonly IProjectCookScopeProvider cookScopeProvider = cookScopeProvider ?? throw new ArgumentNullException(nameof(cookScopeProvider));
    private readonly ISceneDescriptorGenerator sceneDescriptorGenerator = sceneDescriptorGenerator ?? throw new ArgumentNullException(nameof(sceneDescriptorGenerator));
    private readonly IContentImportManifestBuilder manifestBuilder = manifestBuilder ?? throw new ArgumentNullException(nameof(manifestBuilder));
    private readonly IContentImportManifestValidator manifestValidator = manifestValidator ?? throw new ArgumentNullException(nameof(manifestValidator));
    private readonly IEngineContentPipelineApi engineContentPipelineApi = engineContentPipelineApi ?? throw new ArgumentNullException(nameof(engineContentPipelineApi));

    /// <inheritdoc />
    public Task<ContentCookResult> CookCurrentSceneAsync(AssetPath sceneAssetUri, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sceneAssetUri);

        // A request may name a source file; the cook, its run and its results name the asset the file stands for.
        sceneAssetUri = AssetFileTypes.ToIdentity(sceneAssetUri);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.CurrentScene, sceneAssetUri) { CoalescePending = true, OriginContext = this.session.Context },
            (operation, token) => this.CookCurrentSceneCoreAsync(operation, sceneAssetUri, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookAssetAsync(AssetPath assetUri, CancellationToken cancellationToken, ProjectContext? expectedProject = null)
    {
        ArgumentNullException.ThrowIfNull(assetUri);

        // A request may name a source file; the cook, its run and its results name the asset the file stands for.
        assetUri = AssetFileTypes.ToIdentity(assetUri);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, assetUri) { CoalescePending = true, OriginContext = expectedProject ?? this.session.Context },
            (operation, token) => expectedProject is not null && (operation.Project.ProjectId != expectedProject.ProjectId
                || operation.Project.ProjectRoot != expectedProject.ProjectRoot)
                ? Task.FromResult(CreateFailedCook(operation, CookTargetKind.Asset, new InvalidOperationException("The originating project is no longer active.")))
                : this.CookAssetCoreAsync(operation, assetUri, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookAssetsAsync(IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(assetUris);

        // A request may name source files; the cook, its run and its results name the assets they stand for.
        AssetPath[] assets = [.. assetUris.Select(AssetFileTypes.ToIdentity).Distinct()];
        return assets switch
        {
            [] => throw new ArgumentException("Cooking assets together needs at least one asset.", nameof(assetUris)),
            [var only] => this.CookAssetAsync(only, cancellationToken),
            _ => this.cookCoordinator.RunCookAsync(
                new(CookTargetKind.Changes, ScopeUri: null) { Assets = assets, OriginContext = this.session.Context },
                (operation, token) => this.CookAssetsCoreAsync(operation, assets, isAutomatic: false, token),
                cancellationToken),
        };
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookFolderAsync(AssetPath folderUri, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(folderUri);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Folder, folderUri) { CoalescePending = true, OriginContext = this.session.Context },
            (operation, token) => this.CookFolderCoreAsync(operation, folderUri, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookProjectAsync(CancellationToken cancellationToken)
        => this.cookCoordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null) { CoalescePending = true, OriginContext = this.session.Context }, this.CookProjectCoreAsync, cancellationToken);

    internal static bool IsCookableDescriptorFile(ProjectContext project, string path)
        => ContentCookAssetKinds.Of(path) switch
        {
            // A script is a source, not a descriptor: it cooks with the scenes that use it, or on its own when it changes.
            ContentCookAssetKind.Unknown or ContentCookAssetKind.Script => false,
            ContentCookAssetKind.ForeignSource => Import.ModelRecordLookup.TryFind(project, FileSystemPath.From(path)) is not null,
            _ => !AssetFileTypes.Is(path, AssetFileCategory.Cooked),
        };

    private static ContentCookResult MergeProjectResults(
        Guid operationId,
        IReadOnlyList<ContentCookResult> results)
    {
        var cookedAssets = results.SelectMany(static result => result.CookedAssets).ToList();
        var diagnostics = results.SelectMany(static result => result.Diagnostics).ToList();
        var validationDiagnostics = results
            .SelectMany(static result => result.Validation?.Diagnostics ?? [])
            .ToList();
        var validation = new CookValidationResult(
            CookedRoot: string.Join(Path.PathSeparator, results.Select(static result => result.Validation?.CookedRoot).Where(static root => root is not null)),
            Succeeded: results.All(static result => result.Validation?.Succeeded == true),
            validationDiagnostics);
        var inspections = results
            .Select(static result => result.Inspection)
            .Where(static inspection => inspection is { Succeeded: true })
            .Cast<CookInspectionResult>()
            .ToList();
        var inspection = inspections.Count == 0
            ? null
            : new CookInspectionResult(
                CookedRoot: string.Join(Path.PathSeparator, inspections.Select(static item => item.CookedRoot)),
                Succeeded: true,
                SourceIdentity: null,
                inspections.SelectMany(static item => item.Assets).ToList(),
                inspections.SelectMany(static item => item.Files).ToList(),
                Diagnostics: []);

        return new ContentCookResult(
            operationId,
            CookTargetKind.Project,
            ReduceStatus(results.Select(static result => result.Status)),
            NormalizeDiagnostics(operationId, diagnostics),
            cookedAssets,
            inspection,
            validation)
        {
            ModelImports = results.SelectMany(static result => result.ModelImports)
                .ToImmutableDictionary(static entry => entry.Key, static entry => entry.Value, StringComparer.Ordinal),
            AuxiliaryFilesBySource = results.SelectMany(static result => result.AuxiliaryFilesBySource)
                .ToImmutableDictionary(static entry => entry.Key, static entry => entry.Value),
        };
    }

    private static OperationStatus ReduceStatus(IEnumerable<OperationStatus> statuses)
    {
        var statusList = statuses.ToList();
        return statusList.Exists(static status => status == OperationStatus.Failed)
            ? statusList.Exists(static status => status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings)
                ? OperationStatus.PartiallySucceeded
                : OperationStatus.Failed
            : statusList.Exists(static status => status == OperationStatus.SucceededWithWarnings)
                ? OperationStatus.SucceededWithWarnings
                : OperationStatus.Succeeded;
    }

    private static bool HasError(IReadOnlyList<DiagnosticRecord> diagnostics)
        => diagnostics.Any(static diagnostic => diagnostic.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal);

    private static List<DiagnosticRecord> CreateSourceMissingDiagnostics(
        ProjectContext project,
        Guid operationId,
        IEnumerable<ContentCookInput> inputs)
        => inputs
            .Where(input => IsGone(project, input))
            .Select(input => new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.AssetImport,
                Severity = DiagnosticSeverity.Error,
                Code = AssetImportDiagnosticCodes.SourceMissing,
                Message = $"Cook source is missing: {input.SourceRelativePath}.",
                AffectedVirtualPath = input.AssetUri.VirtualPath,
                AffectedPath = input.SourceAbsolutePath.FullPath,
            })
            .ToList();

    private static List<DiagnosticRecord> NormalizeDiagnostics(
        Guid operationId,
        IEnumerable<DiagnosticRecord> diagnostics)
        => diagnostics
            .Select(diagnostic => diagnostic.OperationId == operationId
                ? diagnostic
                : diagnostic with { OperationId = operationId })
            .ToList();

    private static OperationStatus GetStatus(
        IReadOnlyList<DiagnosticRecord> descriptorDiagnostics,
        CookValidationResult validation)
        => !validation.Succeeded ? OperationStatus.Failed
            : descriptorDiagnostics.Any(static diagnostic => diagnostic.Severity == DiagnosticSeverity.Warning)
                ? OperationStatus.SucceededWithWarnings
                : OperationStatus.Succeeded;

    // A container's products are the ones its job reported; any other source's only product is its own identity.
    private static bool Produced(ContentCookInput input, CookedAssetEntry asset, NativeImportResult import)
        => input.Kind is ContentCookAssetKind.ForeignSource or ContentCookAssetKind.Input
            ? import.ProductsBySource.TryGetValue(input.SourceRelativePath, out var products) && products.Contains(asset.VirtualPath)
            : string.Equals(input.AssetUri.VirtualPath, asset.VirtualPath, StringComparison.Ordinal);

    private static List<ContentCookedAsset> CreateCookedAssets(
        ContentCookScope scope,
        CookInspectionResult inspection,
        CookedInventoryReport inventory,
        NativeImportResult import)
    {
        var result = new List<ContentCookedAsset>();
        foreach (var asset in inspection.Assets)
        {
            if (scope.Inputs.FirstOrDefault(input => Produced(input, asset, import)) is { } input)
            {
                result.Add(new(input.AssetUri, AssetPath.Parse(asset.VirtualPath), asset.Kind, input.MountName, asset.VirtualPath)
                {
                    DescriptorRelativePath = asset.DescriptorRelativePath,
                });
            }
        }

        if (inventory.IsValid)
        {
            foreach (var input in scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.Texture))
            {
                var virtualPath = input.AssetUri.VirtualPath;
                var descriptor = inventory.Resources.SingleOrDefault(resource => string.Equals(resource.Kind, "texture", StringComparison.Ordinal) && string.Equals(resource.VirtualPath, virtualPath, StringComparison.Ordinal))
                    ?? throw new InvalidDataException($"Native cooking did not produce texture '{virtualPath}'.");
                result.Add(new(input.AssetUri, AssetPath.Parse(virtualPath), ContentCookAssetKind.Texture, input.MountName, virtualPath)
                {
                    DescriptorRelativePath = descriptor.DescriptorPath,
                });
            }
        }

        return result;
    }

    private static ContentCookAssetKind GetAssetKind(AssetPath assetUri)
    {
        // An input source is named by its identity (Player.input) and cooks from its source, which the resolver finds.
        var kind = ContentCookAssetKinds.Of(assetUri.Name);
        return kind != ContentCookAssetKind.Unknown ? kind
            : ContentCookAssetKinds.Of(assetUri.WithAuthoringSuffix().Name) == ContentCookAssetKind.Input ? ContentCookAssetKind.Input
            : throw new ArgumentException($"Unsupported content cook asset URI '{assetUri}'.", nameof(assetUri));
    }

    private static string GetMountName(string virtualPath)
    {
        var path = virtualPath.TrimStart('/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        return slash <= 0 ? path : path[..slash];
    }

    private static ContentCookInput ResolveInput(
        ProjectContext project,
        AssetPath assetUri,
        ContentCookAssetKind kind,
        ContentCookInputRole role)
    {
        var input = CookInputResolver.Resolve(project, assetUri, role);
        return input.Kind == kind ? input : throw new ArgumentException("The asset kind does not match its source identity.", nameof(assetUri));
    }

    private static List<ContentCookInput> ResolveFolderInputs(ProjectContext project, AssetPath folderUri)
    {
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, folderUri.Mount, StringComparison.Ordinal))
                    ?? throw new InvalidOperationException($"Project does not declare authoring mount '{folderUri.Mount}'.");
        if (ProjectOutputFolders.Contains(mount))
        {
            return [];
        }

        var absoluteFolder = project.Find(folderUri)!.File.FullPath;
        var files = project.FileSystem;
        return !files.Directory.Exists(absoluteFolder) ? [] : files.Directory.EnumerateFiles(absoluteFolder, "*", SearchOption.AllDirectories)
            .Where(file => IsCookableDescriptorFile(project, file))
            .Select(file => ResolveFileInput(project, mount, file, ContentCookInputRole.Primary))
            .OrderBy(static input => input.SourceRelativePath, StringComparer.Ordinal)
            .ToList();
    }

    private static List<ContentCookInput> ResolveMountInputs(
        ProjectContext project,
        ProjectMountPoint mount)
    {
        var mountRoot = project.ProjectRoot.Combine(mount.RelativePath);
        var files = project.FileSystem;
        return !files.Directory.Exists(mountRoot.FullPath) ? [] : files.Directory.EnumerateFiles(mountRoot.FullPath, "*", SearchOption.AllDirectories)
            .Where(file => IsCookableDescriptorFile(project, file))
            .Select(file => ResolveFileInput(project, mount, file, ContentCookInputRole.Primary))
            .OrderBy(static input => input.SourceRelativePath, StringComparer.Ordinal)
            .ToList();
    }

    private static ContentCookInput ResolveFileInput(
        ProjectContext project,
        ProjectMountPoint mount,
        string sourceAbsolutePath,
        ContentCookInputRole role)
    {
        var assetUri = AssetPath.Create(mount.Name, FileSystemPath.From(sourceAbsolutePath).GetPathBelow(project.ProjectRoot.Combine(mount.RelativePath)));
        return CookInputResolver.Resolve(project, assetUri, role);
    }

    private static ContentImportManifest PruneReusableJobs(ContentCookScope scope, ContentImportManifest manifest, Guid sourceKey)
    {
        var skippedSources = scope.Inputs.GroupBy(static input => input.SourceRelativePath, StringComparer.Ordinal)
            .Where(group => group.All(input => scope.ReusableSources.Contains(input.AssetUri)))
            .Select(static group => group.Key).ToHashSet(StringComparer.Ordinal);
        var keptJobs = manifest.Jobs.Where(job => !skippedSources.Contains(job.AttributedSource)).ToArray();

        // A script sidecar patches its scene, so it runs only with its scene's job.
        keptJobs = [.. keptJobs.Where(job => job.Source is not null
            || keptJobs.Any(owner => owner.Source is not null && string.Equals(owner.Source, job.OwnerSource, StringComparison.Ordinal)))];
        var keptIds = keptJobs.Select(static job => job.Id).ToHashSet(StringComparer.Ordinal);
        return manifest with
        {
            SourceKey = sourceKey,
            Jobs = keptJobs.Select(job => job with
            {
                DependsOn = job.DependsOn.Where(keptIds.Contains).ToArray(),
                CookedContextRoots = job.Type is "material-descriptor" or "scene-descriptor" or "geometry-descriptor" or "script-sidecar" or "input" && scope.CookedContextRoots.Count > 0 ? scope.CookedContextRoots : null,
            }).ToArray(),
        };
    }

    private static ContentCookResult CreateFailedImport(Guid operationId, CookTargetKind targetKind, List<DiagnosticRecord> diagnostics)
        => new(
            operationId,
            targetKind,
            OperationStatus.Failed,
            NormalizeDiagnostics(operationId, diagnostics),
            CookedAssets: [],
            Inspection: null,
            Validation: null);

    private static IEnumerable<DiagnosticRecord> MapSourceDiagnostics(ContentCookScope scope, IEnumerable<DiagnosticRecord> nativeDiagnostics)
        => nativeDiagnostics.Select(issue =>
        {
            var input = scope.Inputs.FirstOrDefault(input => issue.AffectedPath is { } affected && input.SourceAbsolutePath == FileSystemPath.From(affected));
            return input is null ? issue : issue with
            {
                AffectedPath = Path.Combine(scope.Project.ProjectRoot.FullPath, input.SourceRelativePath),
                AffectedVirtualPath = input.AssetUri.VirtualPath,
            };
        });

    private static ContentCookResult WithAuxiliaryFiles(ContentCookResult validated, ContentCookScope scope, NativeImportResult importResult)
    {
        var resources = validated.NativeInventory?.Files.Where(static file => file.Value.Kind is { } kind && LooseCookedFileKinds.IsPathKeyed(kind))
            .Select(static file => file.Key).ToHashSet(StringComparer.Ordinal) ?? new HashSet<string>(StringComparer.Ordinal);
        return validated with
        {
            ModelImports = importResult.ModelImports,
            AuxiliaryFilesBySource = scope.Inputs.Where(input => importResult.OutputsBySource.ContainsKey(input.SourceRelativePath))
                .ToImmutableDictionary(
                    static input => input.AssetUri,
                    input => importResult.OutputsBySource[input.SourceRelativePath].Where(resources.Contains).ToImmutableArray()),
        };
    }

    private Task<ContentCookResult> CookCurrentSceneCoreAsync(ContentCookOperation operation, AssetPath sceneAssetUri, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateSceneScope(operation.Project, sceneAssetUri) with { ScopeUri = sceneAssetUri }], CookTargetKind.CurrentScene, cancellationToken);

    private Task<ContentCookResult> CookAssetCoreAsync(ContentCookOperation operation, AssetPath assetUri, CancellationToken cancellationToken, bool allowImportedSourceChanges = true, bool isAutomatic = false)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateAssetScope(operation.Project, assetUri, allowImportedSourceChanges, isAutomatic)], CookTargetKind.Asset, cancellationToken);

    // Each asset keeps the scope it has when cooked alone, so a requested product still resolves to its container.
    private Task<ContentCookResult> CookAssetsCoreAsync(ContentCookOperation operation, IReadOnlyList<AssetPath> assetUris, bool isAutomatic, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(
            operation,
            () => [.. assetUris.Select(assetUri => this.CreateAssetScope(operation.Project, assetUri, allowImportedSourceChanges: !isAutomatic, isAutomatic))],
            assetUris.Count == 1 ? CookTargetKind.Asset : CookTargetKind.Changes,
            cancellationToken);

    private Task<ContentCookResult> CookFolderCoreAsync(ContentCookOperation operation, AssetPath folderUri, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateScope(operation.Project, ResolveFolderInputs(operation.Project, folderUri), CookTargetKind.Folder) with { ScopeUri = folderUri }], CookTargetKind.Folder, cancellationToken);

    private Task<ContentCookResult> CookProjectCoreAsync(ContentCookOperation operation, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(
            operation,
            () => operation.Project.AuthoringMounts.Where(static mount => !ProjectOutputFolders.Contains(mount))
                .Select(mount => this.CreateScope(operation.Project, ResolveMountInputs(operation.Project, mount), CookTargetKind.Project)).ToArray(),
            CookTargetKind.Project,
            cancellationToken);

    private static void ReportPreparing(IEnumerable<ContentCookInput> inputs)
    {
        foreach (var input in inputs)
        {
            CookRunContext.Report(new(Asset: new(input.AssetUri, input.Kind, CookAssetState.Preparing)));
        }
    }

    private Task<ContentCookResult> CookMixedInputsAsync(Guid operationId, ContentCookScope scope, CancellationToken cancellationToken)
        => scope.Inputs.Any(static input => input.Kind == ContentCookAssetKind.ForeignSource)
            ? this.CookWithImportedSourcesAsync(operationId, scope, cancellationToken)
            : scope.Inputs.Any(static input => input.Kind == ContentCookAssetKind.Scene)
            ? this.CookSceneInputsAsync(operationId, scope, cancellationToken)
            : this.CookResolvedInputsAsync(operationId, scope, diagnostics: [], cancellationToken);

    private async Task<ContentCookResult> CookSceneInputsAsync(Guid operationId, ContentCookScope scope, CancellationToken cancellationToken)
    {
        var descriptors = scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.Scene)
            .Select(input => scope.SceneDescriptors.TryGetValue(input.AssetUri, out var descriptor)
                ? descriptor : throw new InvalidOperationException("The scene has no accepted source projection.")).ToArray();
        var inputs = scope.Inputs.Concat(descriptors.SelectMany(static descriptor => descriptor.Dependencies))
            .DistinctBy(static input => input.AssetUri).ToArray();
        var prepared = scope with { Inputs = inputs };
        var manifest = this.manifestBuilder.BuildSceneManifests(prepared, descriptors);
        var result = await this.ExecuteManifestAsync(
            operationId,
            scope.TargetKind,
            prepared,
            manifest,
            descriptors.SelectMany(static descriptor => descriptor.Diagnostics).ToArray(),
            cancellationToken).ConfigureAwait(false);
        return descriptors.Any(static descriptor => descriptor.ScriptBindings.Count > 0)
            ? result with { Diagnostics = [.. result.Diagnostics.Select(issue => MapScriptSidecarDiagnostic(scope, descriptors, issue))] }
            : result;
    }

    private Task<ContentCookResult> CookResolvedInputsAsync(
        Guid operationId,
        ContentCookScope scope,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
        => this.ExecuteManifestAsync(operationId, scope.TargetKind, scope, this.manifestBuilder.BuildManifest(scope), diagnostics, cancellationToken);

    private async Task<ContentCookResult> ExecuteManifestAsync(
        Guid operationId,
        CookTargetKind targetKind,
        ContentCookScope scope,
        ContentImportManifest manifest,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        var output = scope.Output ?? throw new InvalidOperationException("The cook has no generation owner.");
        manifest = PruneReusableJobs(scope, manifest, output.SourceKey);
        scope = scope with { Inputs = scope.Inputs.Where(input => !scope.ReusableSources.Contains(input.AssetUri)).ToArray() };
        var manifestDiagnostics = this.manifestValidator.Validate(operationId, manifest);
        if (HasError(manifestDiagnostics))
        {
            return CreateFailedImport(operationId, targetKind, diagnostics.Concat(manifestDiagnostics).ToList());
        }

        await output.PrepareWriteAsync().ConfigureAwait(false);
        var importResult = await this.ImportManifestAsync(operationId, scope, manifest, cancellationToken)
            .ConfigureAwait(false);
        var allDiagnostics = diagnostics.Concat(MapSourceDiagnostics(scope, importResult.Diagnostics)).ToList();
        if (!importResult.Succeeded)
        {
            return CreateFailedImport(operationId, targetKind, allDiagnostics);
        }

        var validated = await this.ValidateImportedOutputAsync(operationId, targetKind, scope, manifest, allDiagnostics, importResult, cancellationToken).ConfigureAwait(false);
        return WithAuxiliaryFiles(validated, scope, importResult);
    }

    private async Task<ContentCookResult> ValidateImportedOutputAsync(Guid operationId, CookTargetKind targetKind, ContentCookScope scope, ContentImportManifest manifest, List<DiagnosticRecord> allDiagnostics, NativeImportResult import, CancellationToken cancellationToken)
    {
        CookRunContext.Report(new(Message: "Verifying cooked inventory.", State: CookRunState.Validating));
        CookedInventoryReport inventory;
        try
        {
            inventory = await this.ReadVerifiedInventoryAsync(scope, manifest.Output, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or System.Text.Json.JsonException or System.ComponentModel.Win32Exception)
        {
            var diagnostic = new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.ContentPipeline,
                Severity = DiagnosticSeverity.Error,
                Code = ContentPipelineDiagnosticCodes.InspectFailed,
                Message = "The cooked output could not be inspected.",
                TechnicalMessage = error.Message,
                ExceptionType = error.GetType().FullName,
                AffectedPath = manifest.Output,
            };
            allDiagnostics.Add(diagnostic);
            return new ContentCookResult(
                operationId,
                targetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, allDiagnostics),
                CookedAssets: [],
                new CookInspectionResult(manifest.Output, Succeeded: false, SourceIdentity: null, [], [], [diagnostic]),
                Validation: null);
        }

        var inspection = inventory.ToInspection(manifest.Output);
        var validation = inventory.ToValidation(manifest.Output);
        allDiagnostics.AddRange(validation.Diagnostics);
        var proof = inventory.IsValid ? new CookProvenance.Root(scope.Inputs[0].MountName, inventory.SourceKey, inventory.IndexSha256) : null;
        return new ContentCookResult(
            operationId,
            targetKind,
            GetStatus(allDiagnostics, validation),
            NormalizeDiagnostics(operationId, allDiagnostics),
            CreateCookedAssets(scope, inspection, inventory, import),
            inspection,
            validation)
        {
            VerifiedRoot = proof,
            NativeInventory = inventory,
        };
    }

    private async Task<CookedInventoryReport> ReadVerifiedInventoryAsync(ContentCookScope scope, string output, CancellationToken cancellationToken)
    {
        var outputLease = await CookOutputReadLease.AcquireAsync(output, cancellationToken).ConfigureAwait(false);
        try
        {
            var inventory = await outputLease.ReadInventoryAsync(this.engineContentPipelineApi, cancellationToken, scope.Artifacts).ConfigureAwait(false);
            if (inventory.IsValid)
            {
                (scope.Output ?? throw new InvalidOperationException("The cook has no generation owner.")).AcceptVerification(outputLease, inventory);
                outputLease = null;
            }

            return inventory;
        }
        finally
        {
            if (outputLease is not null)
            {
                await outputLease.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private Task<NativeImportResult> ImportManifestAsync(Guid operationId, ContentCookScope scope, ContentImportManifest manifest, CancellationToken cancellationToken)
    {
        foreach (var input in scope.Inputs)
        {
            CookRunContext.Report(new(Asset: new(input.AssetUri, input.Kind, CookAssetState.Cooking)));
        }

        CookRunContext.Report(new(Message: "Cooking content.", State: CookRunState.Cooking));
        var execution = new ContentImportExecution(
            operationId,
            scope.InputRoot,
            Path.Combine(scope.Project.ProjectRoot.FullPath, ".build", "cook", operationId.ToString("N")),
            manifest)
        { Artifacts = scope.Artifacts, CapturedInputs = scope.CapturedInputs };
        return this.engineContentPipelineApi.ImportAsync(execution, cancellationToken);
    }

    private ContentCookScope CreateSceneScope(ProjectContext project, AssetPath sceneAssetUri)
    {
        var input = ResolveInput(project, sceneAssetUri, ContentCookAssetKind.Scene, ContentCookInputRole.Primary);
        return new ContentCookScope(
            project,
            this.cookScopeProvider.CreateScope(project),
            [input],
            CookTargetKind.CurrentScene);
    }

    private ContentCookScope CreateScope(
        ProjectContext project,
        IReadOnlyList<ContentCookInput> inputs,
        CookTargetKind targetKind)
        => new(project, this.cookScopeProvider.CreateScope(project), inputs, targetKind);

    private ContentCookScope CreateAssetScope(ProjectContext project, AssetPath assetUri, bool allowImportedSourceChanges, bool isAutomatic)
        => this.CreateScope(project, [ResolveInput(project, assetUri, GetAssetKind(assetUri), ContentCookInputRole.Primary)], CookTargetKind.Asset) with
        {
            ScopeUri = assetUri,
            AllowImportedSourceChanges = allowImportedSourceChanges,
            IsAutomatic = isAutomatic,
        };
}
