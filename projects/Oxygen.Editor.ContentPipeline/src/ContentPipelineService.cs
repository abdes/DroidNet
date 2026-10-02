// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Default explicit editor content-pipeline workflow service.
/// </summary>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="cookCoordinator">The shared project writer and lifetime coordinator.</param>
/// <param name="cookScopeProvider">The project cook scope provider.</param>
/// <param name="sceneDescriptorGenerator">The scene descriptor generator.</param>
/// <param name="manifestBuilder">The import manifest builder.</param>
/// <param name="manifestValidator">The import manifest validator.</param>
/// <param name="engineContentPipelineApi">The engine content-pipeline adapter.</param>
/// <param name="cookDocuments">The registered saved-document owners.</param>
/// <param name="nativeCompatibility">The compatible producer identity and file ownership.</param>
/// <param name="files">Atomic storage for retained source settings.</param>
/// <param name="publication">The shared journal and runtime-publication owner.</param>
public sealed partial class ContentPipelineService(
    IProjectContextService projectContextService,
    IContentCookCoordinator cookCoordinator,
    IProjectCookScopeProvider cookScopeProvider,
    ISceneDescriptorGenerator sceneDescriptorGenerator,
    IContentImportManifestBuilder manifestBuilder,
    IContentImportManifestValidator manifestValidator,
    IEngineContentPipelineApi engineContentPipelineApi,
    ICookDocumentRegistry cookDocuments,
    INativeCompatibilityService nativeCompatibility,
    DroidNet.Storage.IAtomicFileStore files,
    Publication.CookPublicationService publication) : IContentPipelineService
{

    private readonly INativeCompatibilityService nativeCompatibility = nativeCompatibility;
    private readonly Publication.CookPublicationService publication = publication ?? throw new ArgumentNullException(nameof(publication));

    private readonly IProjectContextService projectContextService = projectContextService ?? throw new ArgumentNullException(nameof(projectContextService));
    private readonly IContentCookCoordinator cookCoordinator = cookCoordinator ?? throw new ArgumentNullException(nameof(cookCoordinator));
    private readonly IProjectCookScopeProvider cookScopeProvider = cookScopeProvider ?? throw new ArgumentNullException(nameof(cookScopeProvider));
    private readonly ISceneDescriptorGenerator sceneDescriptorGenerator = sceneDescriptorGenerator ?? throw new ArgumentNullException(nameof(sceneDescriptorGenerator));
    private readonly IContentImportManifestBuilder manifestBuilder = manifestBuilder ?? throw new ArgumentNullException(nameof(manifestBuilder));
    private readonly IContentImportManifestValidator manifestValidator = manifestValidator ?? throw new ArgumentNullException(nameof(manifestValidator));
    private readonly IEngineContentPipelineApi engineContentPipelineApi = engineContentPipelineApi ?? throw new ArgumentNullException(nameof(engineContentPipelineApi));

    /// <inheritdoc />
    public Task<ContentCookResult> CookCurrentSceneAsync(Uri sceneAssetUri, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(sceneAssetUri);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.CurrentScene, sceneAssetUri) { CoalescePending = true, OriginContext = this.projectContextService.ActiveProject },
            (operation, token) => this.CookCurrentSceneCoreAsync(operation, sceneAssetUri, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookAssetAsync(Uri assetUri, CancellationToken cancellationToken, ProjectContext? expectedProject = null)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, assetUri) { CoalescePending = true, OriginContext = expectedProject ?? this.projectContextService.ActiveProject },
            (operation, token) => expectedProject is not null && (operation.Project.ProjectId != expectedProject.ProjectId
                || !string.Equals(operation.Project.ProjectRoot, expectedProject.ProjectRoot, StringComparison.OrdinalIgnoreCase))
                ? Task.FromResult(CreateFailedCook(operation, CookTargetKind.Asset, new InvalidOperationException("The originating project is no longer active.")))
                : this.CookAssetCoreAsync(operation, assetUri, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookFolderAsync(Uri folderUri, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(folderUri);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Folder, folderUri) { CoalescePending = true, OriginContext = this.projectContextService.ActiveProject },
            (operation, token) => this.CookFolderCoreAsync(operation, folderUri, token),
            cancellationToken);
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookProjectAsync(CancellationToken cancellationToken)
        => this.cookCoordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null) { CoalescePending = true, OriginContext = this.projectContextService.ActiveProject }, this.CookProjectCoreAsync, cancellationToken);

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
            MaterialSlotProvenance = results.SelectMany(static result => result.MaterialSlotProvenance)
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
        Guid operationId,
        IEnumerable<ContentCookInput> inputs)
        => inputs
            .Where(static input => !File.Exists(input.SourceAbsolutePath))
            .Select(input => new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.AssetImport,
                Severity = DiagnosticSeverity.Error,
                Code = AssetImportDiagnosticCodes.SourceMissing,
                Message = $"Cook source is missing: {input.SourceRelativePath}.",
                AffectedVirtualPath = input.AssetUri.AbsolutePath,
                AffectedPath = input.SourceAbsolutePath,
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

    private static List<ContentCookedAsset> CreateCookedAssets(
        ContentCookScope scope,
        CookInspectionResult inspection,
        CookedInventoryReport inventory,
        NativeImportResult import)
    {
        var result = new List<ContentCookedAsset>();
        foreach (var asset in inspection.Assets)
        {
            var input = scope.Inputs.FirstOrDefault(input => input.OwnsOutput(asset.VirtualPath));
            if (input is not null && (input.Kind != ContentCookAssetKind.ForeignSource
                || (asset.DescriptorRelativePath is not null
                    && import.OutputsBySource.TryGetValue(input.SourceRelativePath, out var outputs)
                    && outputs.Contains(asset.DescriptorRelativePath, StringComparer.OrdinalIgnoreCase))))
            {
                result.Add(new(input.AssetUri, ToAssetUri(asset.VirtualPath), asset.Kind, input.MountName, asset.VirtualPath)
                {
                    DescriptorRelativePath = asset.DescriptorRelativePath,
                });
            }
        }

        if (inventory.IsValid)
        {
            foreach (var input in scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.Texture))
            {
                var virtualPath = input.OutputVirtualPath ?? throw new InvalidDataException("A texture requires an explicit output identity.");
                var prefix = ContentPipelinePaths.GetVirtualMountRoot(input.MountName) + "/";
                var relative = virtualPath.StartsWith(prefix, StringComparison.Ordinal) ? virtualPath[prefix.Length..]
                    : throw new InvalidDataException("Texture output is outside its mount.");
                var descriptor = inventory.Resources.SingleOrDefault(resource => string.Equals(resource.Kind, "texture", StringComparison.Ordinal) && string.Equals(resource.DescriptorPath, relative, StringComparison.Ordinal))
                    ?? throw new InvalidDataException($"Native cooking did not produce texture '{virtualPath}'.");
                result.Add(new(input.AssetUri, ToAssetUri(virtualPath), ContentCookAssetKind.Texture, input.MountName, virtualPath)
                {
                    DescriptorRelativePath = descriptor.DescriptorPath,
                });
            }
        }

        return result;
    }

    private static Uri ToAssetUri(string mountName, string mountRelativePath)
        => new($"{AssetUris.Scheme}:///{mountName}/{mountRelativePath.Replace('\\', '/')}");

    private static Uri ToAssetUri(string virtualPath)
        => new($"{AssetUris.Scheme}://{(virtualPath.StartsWith('/') ? virtualPath : "/" + virtualPath)}");

    private static ContentCookAssetKind GetAssetKind(Uri assetUri)
    {
        var path = Uri.UnescapeDataString(assetUri.AbsolutePath);
        return path switch
        {
            _ when path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".otex", StringComparison.OrdinalIgnoreCase) => ContentCookAssetKind.Texture,
            _ when path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".omat", StringComparison.OrdinalIgnoreCase) => ContentCookAssetKind.Material,
            _ when path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".ogeo", StringComparison.OrdinalIgnoreCase) => ContentCookAssetKind.Geometry,
            _ when path.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".oscene", StringComparison.OrdinalIgnoreCase) => ContentCookAssetKind.Scene,
            _ when path.EndsWith(".gltf", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".glb", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".fbx", StringComparison.OrdinalIgnoreCase) => ContentCookAssetKind.ForeignSource,
            _ => throw new ArgumentException($"Unsupported content cook asset URI '{assetUri}'.", nameof(assetUri)),
        };
    }

    private static string GetMountName(string virtualPath)
    {
        var path = virtualPath.TrimStart('/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        return slash <= 0 ? path : path[..slash];
    }

    private static ContentCookInput ResolveInput(
        ProjectContext project,
        Uri assetUri,
        ContentCookAssetKind kind,
        ContentCookInputRole role)
    {
        var input = CookInputResolver.Resolve(project, assetUri, role);
        return input.Kind == kind ? input : throw new ArgumentException("The asset kind does not match its source identity.", nameof(assetUri));
    }

    private static string GetExpectedExtension(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Texture => ".otex",
            ContentCookAssetKind.Material => ".omat",
            ContentCookAssetKind.Geometry => ".ogeo",
            ContentCookAssetKind.Scene => ".oscene",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported cook input kind."),
        };

    private static bool IsCookableDescriptorFile(string path)
        => path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
           || path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
           || path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
           || path.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase)
           || ((path.EndsWith(".gltf", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".glb", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".fbx", StringComparison.OrdinalIgnoreCase))
               && File.Exists(path + Import.NativeSceneImportSettings.SidecarSuffix));

    private static List<ContentCookInput> ResolveFolderInputs(ProjectContext project, Uri folderUri)
    {
        if (!string.Equals(folderUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("Cook folder URI must be an editor asset URI.", nameof(folderUri));
        }

        var path = Uri.UnescapeDataString(folderUri.AbsolutePath).Trim('/').Replace('\\', '/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        var mountName = slash <= 0 ? path : path[..slash];
        var mountRelativeFolder = slash <= 0 ? string.Empty : path[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase))
                    ?? throw new InvalidOperationException($"Project does not declare authoring mount '{mountName}'.");
        if (IsDerivedRootMount(mount))
        {
            return [];
        }

        var absoluteFolder = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath, mountRelativeFolder));
        return !Directory.Exists(absoluteFolder) ? [] : Directory.EnumerateFiles(absoluteFolder, "*", SearchOption.AllDirectories)
            .Where(IsCookableDescriptorFile)
            .Select(file => ResolveFileInput(project, mount, file, ContentCookInputRole.Primary))
            .OrderBy(static input => input.SourceRelativePath, StringComparer.Ordinal)
            .ToList();
    }

    private static List<ContentCookInput> ResolveMountInputs(
        ProjectContext project,
        ProjectMountPoint mount)
    {
        var mountRoot = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
        return !Directory.Exists(mountRoot) ? [] : Directory.EnumerateFiles(mountRoot, "*", SearchOption.AllDirectories)
            .Where(IsCookableDescriptorFile)
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
        var projectRelativePath = Path.GetRelativePath(project.ProjectRoot, sourceAbsolutePath).Replace('\\', '/');
        var mountRoot = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
        var mountRelativePath = Path.GetRelativePath(mountRoot, sourceAbsolutePath).Replace('\\', '/');
        var assetUri = ToAssetUri(mount.Name, mountRelativePath);
        var kind = GetAssetKind(assetUri);
        return kind == ContentCookAssetKind.ForeignSource ? CookInputResolver.Resolve(project, assetUri, role) : new ContentCookInput(
            assetUri,
            kind,
            mount.Name,
            projectRelativePath,
            sourceAbsolutePath,
            ContentPipelinePaths.ToNativeDescriptorPath(assetUri, GetExpectedExtension(kind)),
            role);
    }

    private static bool IsDerivedRootMount(ProjectMountPoint mount)
    {
        var relativePath = mount.RelativePath.Trim().Replace('\\', '/').Trim('/');
        return string.Equals(relativePath, ".cooked", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".imported", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".build", StringComparison.OrdinalIgnoreCase);
    }

    private Task<ContentCookResult> CookCurrentSceneCoreAsync(ContentCookOperation operation, Uri sceneAssetUri, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateSceneScope(operation.Project, sceneAssetUri) with { ScopeUri = sceneAssetUri }], CookTargetKind.CurrentScene, cancellationToken);

    private Task<ContentCookResult> CookAssetCoreAsync(ContentCookOperation operation, Uri assetUri, CancellationToken cancellationToken, bool allowImportedSourceChanges = true)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateScope(operation.Project, [ResolveInput(operation.Project, assetUri, GetAssetKind(assetUri), ContentCookInputRole.Primary)], CookTargetKind.Asset) with { ScopeUri = assetUri, AllowImportedSourceChanges = allowImportedSourceChanges }], CookTargetKind.Asset, cancellationToken);

    private Task<ContentCookResult> CookFolderCoreAsync(ContentCookOperation operation, Uri folderUri, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateScope(operation.Project, ResolveFolderInputs(operation.Project, folderUri), CookTargetKind.Folder) with { ScopeUri = folderUri }], CookTargetKind.Folder, cancellationToken);

    private Task<ContentCookResult> CookProjectCoreAsync(ContentCookOperation operation, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(
            operation,
            () => operation.Project.AuthoringMounts.Where(static mount => !IsDerivedRootMount(mount))
                .Select(mount => this.CreateScope(operation.Project, ResolveMountInputs(operation.Project, mount), CookTargetKind.Project)).ToArray(),
            CookTargetKind.Project,
            cancellationToken);

    private async Task RequireSavedDocumentsAsync(IEnumerable<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        var discovered = inputs.ToArray();
        foreach (var input in discovered)
        {
            CookRunContext.Report(new(Asset: new(input.AssetUri, input.Kind, CookAssetState.Preparing)));
        }

        using var reads = await cookDocuments.AcquireAsync(discovered.Select(static input => input.SourceAbsolutePath), cancellationToken).ConfigureAwait(false);
        var dirty = reads.Documents.Where(static document => document.IsDirty).ToArray();
        if (dirty.Length != 0)
        {
            throw new CookInputsNeedSaveException(dirty);
        }
    }

    private ProjectContext RequireActiveProject()
        => this.projectContextService.ActiveProject
           ?? throw new InvalidOperationException("Content pipeline requires an active project.");

    private Task<ContentCookResult> CookMixedInputsAsync(Guid operationId, ContentCookScope scope, CancellationToken cancellationToken)
        => scope.Inputs.Any(static input => input.Kind == ContentCookAssetKind.ForeignSource)
            ? this.CookWithImportedSourcesAsync(operationId, scope, cancellationToken)
            : scope.Inputs.Any(static input => input.Kind == ContentCookAssetKind.Scene)
            ? this.CookSceneInputsAsync(operationId, scope, cancellationToken)
            : this.CookResolvedInputsAsync(operationId, scope, diagnostics: [], cancellationToken);

    private Task<ContentCookResult> CookSceneInputsAsync(Guid operationId, ContentCookScope scope, CancellationToken cancellationToken)
    {
        var descriptors = scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.Scene)
            .Select(input => scope.SceneDescriptors.TryGetValue(input.AssetUri, out var descriptor)
                ? descriptor : throw new InvalidOperationException("The scene has no accepted source projection.")).ToArray();
        var inputs = scope.Inputs.Concat(descriptors.SelectMany(static descriptor => descriptor.Dependencies))
            .DistinctBy(static input => input.AssetUri).ToArray();
        var prepared = scope with { Inputs = inputs };
        var manifest = this.manifestBuilder.BuildSceneManifests(prepared, descriptors);
        return this.ExecuteManifestAsync(operationId, scope.TargetKind, prepared, manifest,
            descriptors.SelectMany(static descriptor => descriptor.Diagnostics).ToArray(), cancellationToken);
    }

    private Task<ContentCookResult> CookResolvedInputsAsync(Guid operationId, ContentCookScope scope,
        IReadOnlyList<DiagnosticRecord> diagnostics, CancellationToken cancellationToken)
        => this.ExecuteManifestAsync(operationId, scope.TargetKind, scope, this.manifestBuilder.BuildManifest(scope), diagnostics, cancellationToken);

    private async Task<ContentCookResult> ExecuteManifestAsync(
        Guid operationId,
        CookTargetKind targetKind,
        ContentCookScope scope,
        ContentImportManifest manifest,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        var skippedSources = scope.Inputs.GroupBy(static input => input.SourceRelativePath, StringComparer.Ordinal)
            .Where(group => group.All(input => scope.ReusableSources.Contains(input.AssetUri)))
            .Select(static group => group.Key).ToHashSet(StringComparer.Ordinal);
        var keptJobs = manifest.Jobs.Where(job => !skippedSources.Contains(job.Source)).ToArray();
        var keptIds = keptJobs.Select(static job => job.Id).ToHashSet(StringComparer.Ordinal);
        manifest = manifest with
        {
            SourceKey = scope.Output?.SourceKey ?? throw new InvalidOperationException("The cook has no generation owner."),
            Jobs = keptJobs.Select(job => job with
            {
                DependsOn = job.DependsOn.Where(keptIds.Contains).ToArray(),
                CookedContextRoots = job.Type is "scene-descriptor" or "geometry-descriptor" && scope.CookedContextRoots.Count > 0 ? scope.CookedContextRoots : null,
            }).ToArray(),
        };
        scope = scope with { Inputs = scope.Inputs.Where(input => !scope.ReusableSources.Contains(input.AssetUri)).ToArray() };
        var manifestDiagnostics = this.manifestValidator.Validate(operationId, manifest);
        if (HasError(manifestDiagnostics))
        {
            var allManifestDiagnostics = diagnostics.Concat(manifestDiagnostics).ToList();
            return new ContentCookResult(
                operationId,
                targetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, allManifestDiagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        await scope.Output.PrepareWriteAsync().ConfigureAwait(false);
        var importResult = await this.ImportManifestAsync(operationId, scope, manifest, cancellationToken)
            .ConfigureAwait(false);
        var nativeDiagnostics = importResult.Diagnostics.Select(issue =>
        {
            var input = scope.Inputs.FirstOrDefault(input => string.Equals(input.SourceAbsolutePath, issue.AffectedPath, StringComparison.OrdinalIgnoreCase));
            return input is null ? issue : issue with
            {
                AffectedPath = Path.Combine(scope.Project.ProjectRoot, input.SourceRelativePath),
                AffectedVirtualPath = input.AssetUri.AbsolutePath,
            };
        });
        var allDiagnostics = diagnostics.Concat(nativeDiagnostics).ToList();
        if (!importResult.Succeeded)
        {
            return new ContentCookResult(
                operationId,
                targetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, allDiagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var validated = await this.ValidateImportedOutputAsync(operationId, targetKind, scope, manifest, allDiagnostics, importResult, cancellationToken).ConfigureAwait(false);
        var resources = validated.NativeInventory?.Files.Where(static file => file.Value.Kind == Oxygen.Managed.Assets.Persistence.LooseCooked.V3.FileKind.Auxiliary)
            .Select(static file => file.Key).ToHashSet(StringComparer.Ordinal) ?? [];
        return validated with
        {
            MaterialSlotProvenance = importResult.MaterialSlotProvenance,
            AuxiliaryFilesBySource = scope.Inputs.Where(input => importResult.OutputsBySource.ContainsKey(input.SourceRelativePath))
                .ToImmutableDictionary(
                    static input => input.AssetUri,
                    input => importResult.OutputsBySource[input.SourceRelativePath].Where(resources.Contains).ToImmutableArray()),
        };
    }

    private async Task<ContentCookResult> ValidateImportedOutputAsync(Guid operationId, CookTargetKind targetKind, ContentCookScope scope, ContentImportManifest manifest, List<DiagnosticRecord> allDiagnostics, NativeImportResult import, CancellationToken cancellationToken)
    {
        CookRunContext.Report(new(Message: "Verifying cooked inventory.", State: CookRunState.Validating));
        CookedInventoryReport inventory;
        try
        {
            CookOutputReadLease? outputLease = await CookOutputReadLease.AcquireAsync(manifest.Output, cancellationToken).ConfigureAwait(false);
            try
            {
                inventory = await outputLease.ReadInventoryAsync(this.engineContentPipelineApi, cancellationToken, scope.Artifacts).ConfigureAwait(false);
                if (inventory.IsValid)
                {
                    (scope.Output ?? throw new InvalidOperationException("The cook has no generation owner.")).AcceptVerification(outputLease, inventory);
                    outputLease = null;
                }
            }
            finally
            {
                if (outputLease is not null)
                {
                    await outputLease.DisposeAsync().ConfigureAwait(false);
                }
            }
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
                operationId, targetKind, OperationStatus.Failed,
                NormalizeDiagnostics(operationId, allDiagnostics), CookedAssets: [],
                new CookInspectionResult(manifest.Output, Succeeded: false, SourceIdentity: null, [], [], [diagnostic]),
                Validation: null);
        }

        var inspection = inventory.ToInspection(manifest.Output);
        var validation = inventory.ToValidation(manifest.Output);
        allDiagnostics.AddRange(validation.Diagnostics);
        var proof = inventory.IsValid ? new CookProvenance.Root(scope.Inputs[0].MountName, inventory.SourceKey, inventory.IndexSha256) : null;
        return new ContentCookResult(
            operationId, targetKind, GetStatus(allDiagnostics, validation), NormalizeDiagnostics(operationId, allDiagnostics),
            CreateCookedAssets(scope, inspection, inventory, import), inspection, validation)
        {
            VerifiedRoot = proof,
            NativeInventory = inventory,
        };
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
            Path.Combine(scope.Project.ProjectRoot, ".build", "cook", operationId.ToString("N")),
            manifest) { Artifacts = scope.Artifacts, CapturedInputs = scope.CapturedInputs };
        return this.engineContentPipelineApi.ImportAsync(execution, cancellationToken);
    }

    private ContentCookScope CreateSceneScope(ProjectContext project, Uri sceneAssetUri)
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
}
