// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets.Import.Materials;
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
/// <param name="provenanceFiles">Atomic storage for incremental product evidence.</param>
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
    INativeCompatibilityService? nativeCompatibility = null,
    DroidNet.Storage.IAtomicFileStore? provenanceFiles = null,
    Publication.CookPublicationService? publication = null) : IContentPipelineService
{
    private static readonly System.Text.Json.JsonSerializerOptions NativeDescriptorJsonOptions = new()
    {
        WriteIndented = true,
    };

    private readonly INativeCompatibilityService nativeCompatibility = nativeCompatibility ?? EditorNativeCompatibilityService.ForCooking();
    private readonly CookProvenanceStore provenanceStore = new(provenanceFiles ?? new DroidNet.Storage.Native.NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()));
    private readonly Publication.CookPublicationService publication = publication ?? new(cookCoordinator, projectContextService, provenanceFiles ?? new DroidNet.Storage.Native.NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()));

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
            validation);
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
        IReadOnlyList<string>? outputFiles)
    {
        var result = new List<ContentCookedAsset>();
        foreach (var asset in inspection.Assets)
        {
            var input = scope.Inputs.FirstOrDefault(input => input.OutputVirtualPath is { } output
                && (input.Kind == ContentCookAssetKind.ForeignSource
                    ? asset.VirtualPath.StartsWith(output, StringComparison.Ordinal)
                    : string.Equals(asset.VirtualPath, output, StringComparison.Ordinal)));
            if (input is not null && (input.Kind != ContentCookAssetKind.ForeignSource
                || (asset.DescriptorRelativePath is not null && outputFiles?.Contains(asset.DescriptorRelativePath, StringComparer.OrdinalIgnoreCase) == true)))
            {
                result.Add(new(input.AssetUri, ToAssetUri(asset.VirtualPath), asset.Kind, input.MountName, asset.VirtualPath));
            }
        }

        return result;
    }

    private static Project CreateProject(ProjectContext context)
    {
        var projectInfo = new ProjectInfo(context.ProjectId, context.Name, context.Category, context.ProjectRoot, context.Thumbnail)
        {
            AuthoringMounts = [.. context.AuthoringMounts],
            LocalFolderMounts = [.. context.LocalFolderMounts],
        };
        var project = new Project(projectInfo) { Name = context.Name };
        foreach (var scene in context.Scenes)
        {
            project.Scenes.Add(new Scene(project) { Id = scene.Id, Name = scene.Name });
        }

        return project;
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
            ContentCookAssetKind.Material => ".omat",
            ContentCookAssetKind.Geometry => ".ogeo",
            ContentCookAssetKind.Scene => ".oscene",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported cook input kind."),
        };

    private static bool IsCookableDescriptorFile(string path)
        => path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
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

    private static string GetGeneratedMaterialDescriptorPath(string projectRoot, ContentCookInput input)
    {
        var generatedRelative = input.SourceRelativePath.Replace('\\', '/').TrimStart('/');
        return Path.GetFullPath(Path.Combine(projectRoot, ".pipeline", "Materials", generatedRelative));
    }

    private static NativeMaterialDescriptor ToNativeMaterialDescriptor(ContentCookInput input, MaterialSource material)
    {
        var pbr = material.PbrMetallicRoughness;
        return new NativeMaterialDescriptor(
            string.IsNullOrWhiteSpace(material.Name)
                ? Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(input.SourceRelativePath))
                : material.Name!,
            ToNativeDomain(material.AlphaMode),
            ToNativeAlphaMode(material.AlphaMode),
            new NativeMaterialParameters(
                BaseColor: [pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA],
                Metalness: pbr.MetallicFactor,
                Roughness: pbr.RoughnessFactor,
                DoubleSided: material.DoubleSided,
                AlphaCutoff: material.AlphaMode == MaterialAlphaMode.Mask ? material.AlphaCutoff : null));
    }

    private static string ToNativeDomain(MaterialAlphaMode alphaMode)
        => alphaMode switch
        {
            MaterialAlphaMode.Blend => "alpha_blended",
            MaterialAlphaMode.Mask => "masked",
            _ => "opaque",
        };

    private static string ToNativeAlphaMode(MaterialAlphaMode alphaMode)
        => alphaMode switch
        {
            MaterialAlphaMode.Blend => "blended",
            MaterialAlphaMode.Mask => "masked",
            _ => "opaque",
        };

    private static async Task<PreparedScope> PrepareScopeInputsAsync(
        Guid operationId,
        ContentCookScope scope,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        var prepared = new List<ContentCookInput>(scope.Inputs.Count);
        var allDiagnostics = diagnostics.ToList();
        foreach (var input in scope.Inputs)
        {
            var result = await PrepareInputAsync(operationId, scope, input, cancellationToken).ConfigureAwait(false);
            prepared.Add(result.Input);
            allDiagnostics.AddRange(result.Diagnostics);
        }

        return new PreparedScope(scope with { Inputs = prepared }, allDiagnostics);
    }

    private static async Task<PreparedSceneDescriptors> PrepareSceneDescriptorsAsync(
        Guid operationId,
        ContentCookScope scope,
        List<SceneDescriptorGenerationResult> descriptors,
        CancellationToken cancellationToken)
    {
        var diagnostics = descriptors.SelectMany(static descriptor => descriptor.Diagnostics).ToList();
        var preparedScope = await PrepareScopeInputsAsync(operationId, scope, diagnostics, cancellationToken)
            .ConfigureAwait(false);
        diagnostics = preparedScope.Diagnostics.ToList();

        var preparedDescriptors = new List<SceneDescriptorGenerationResult>(descriptors.Count);
        foreach (var descriptor in descriptors)
        {
            var dependencies = new List<ContentCookInput>(descriptor.Dependencies.Count);
            foreach (var dependency in descriptor.Dependencies)
            {
                var result = await PrepareInputAsync(operationId, scope, dependency, cancellationToken).ConfigureAwait(false);
                dependencies.Add(result.Input);
                diagnostics.AddRange(result.Diagnostics);
            }

            preparedDescriptors.Add(descriptor with { Dependencies = dependencies });
        }

        var inputs = preparedScope.Scope.Inputs
            .Concat(preparedDescriptors.SelectMany(static descriptor => descriptor.Dependencies))
            .DistinctBy(static input => input.OutputVirtualPath, StringComparer.Ordinal)
            .ToList();
        return new PreparedSceneDescriptors(preparedScope.Scope with { Inputs = inputs }, preparedDescriptors, diagnostics);
    }

    private static Task<PreparedInput> PrepareInputAsync(Guid operationId, ContentCookScope scope, ContentCookInput input, CancellationToken cancellationToken)
        => input.Kind == ContentCookAssetKind.Geometry && input.Role != ContentCookInputRole.GeneratedDescriptor
            ? PrepareCapturedGeometryAsync(scope, input, cancellationToken)
            : PrepareMaterialInputAsync(operationId, scope, input, cancellationToken);

    private static async Task<PreparedInput> PrepareMaterialInputAsync(
        Guid operationId,
        ContentCookScope scope,
        ContentCookInput input,
        CancellationToken cancellationToken)
    {
        if (input.Kind != ContentCookAssetKind.Material || input.Role == ContentCookInputRole.GeneratedDescriptor)
        {
            return new PreparedInput(input, Diagnostics: []);
        }

        try
        {
            var generatedAbsolutePath = GetGeneratedMaterialDescriptorPath(scope.InputRoot, input);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(generatedAbsolutePath)!);

            var materialBytes = await File.ReadAllBytesAsync(input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
            var material = MaterialSourceReader.Read(materialBytes);
            var native = ToNativeMaterialDescriptor(input, material);
            var stream = File.Create(generatedAbsolutePath);
            await using (stream.ConfigureAwait(false))
            {
                await System.Text.Json.JsonSerializer.SerializeAsync(
                        stream,
                        native,
                        NativeDescriptorJsonOptions,
                        cancellationToken)
                    .ConfigureAwait(false);
            }

            var generatedRelativePath = Path.GetRelativePath(scope.InputRoot, generatedAbsolutePath)
                .Replace('\\', '/');
            return new PreparedInput(
                input with
                {
                    SourceRelativePath = generatedRelativePath,
                    SourceAbsolutePath = generatedAbsolutePath,
                    Role = ContentCookInputRole.GeneratedDescriptor,
                },
                Diagnostics: []);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or System.Text.Json.JsonException)
        {
            return new PreparedInput(
                input,
                Diagnostics:
                [
                    new DiagnosticRecord
                    {
                        OperationId = operationId,
                        Domain = FailureDomain.ContentPipeline,
                        Severity = DiagnosticSeverity.Error,
                        Code = ContentPipelineDiagnosticCodes.ManifestGenerationFailed,
                        Message = $"Material descriptor generation failed for {input.SourceRelativePath}.",
                        TechnicalMessage = ex.Message,
                        ExceptionType = ex.GetType().FullName,
                        AffectedPath = input.SourceAbsolutePath,
                        AffectedVirtualPath = input.AssetUri.AbsolutePath,
                    },
                ]);
        }
    }

    private Task<ContentCookResult> CookCurrentSceneCoreAsync(ContentCookOperation operation, Uri sceneAssetUri, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateSceneScope(operation.Project, sceneAssetUri)], CookTargetKind.CurrentScene, cancellationToken);

    private Task<ContentCookResult> CookAssetCoreAsync(ContentCookOperation operation, Uri assetUri, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateScope(operation.Project, [ResolveInput(operation.Project, assetUri, GetAssetKind(assetUri), ContentCookInputRole.Primary)], CookTargetKind.Asset)], CookTargetKind.Asset, cancellationToken);

    private Task<ContentCookResult> CookFolderCoreAsync(ContentCookOperation operation, Uri folderUri, CancellationToken cancellationToken)
        => this.CookCapturedScopesAsync(operation, () => [this.CreateScope(operation.Project, ResolveFolderInputs(operation.Project, folderUri), CookTargetKind.Folder)], CookTargetKind.Folder, cancellationToken);

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

    private async Task<ContentCookResult> CookSceneInputsAsync(
        Guid operationId,
        ContentCookScope scope,
        CancellationToken cancellationToken)
    {
        var sceneInputs = scope.Inputs.Where(static item => item.Kind == ContentCookAssetKind.Scene).ToList();
        var missingSceneDiagnostics = CreateSourceMissingDiagnostics(operationId, sceneInputs);
        if (missingSceneDiagnostics.Count > 0)
        {
            return new ContentCookResult(
                operationId,
                scope.TargetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, missingSceneDiagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var descriptors = await this.GenerateSceneDescriptorsAsync(scope, sceneInputs, cancellationToken).ConfigureAwait(false);
        var diagnostics = descriptors.SelectMany(static descriptor => descriptor.Diagnostics).ToList();
        diagnostics.AddRange(CreateSourceMissingDiagnostics(
            operationId,
            descriptors.SelectMany(static descriptor => descriptor.Dependencies)));
        if (HasError(diagnostics))
        {
            return new ContentCookResult(
                operationId,
                scope.TargetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, diagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var descriptorInputs = scope.Inputs.Where(static input => input.Kind is not ContentCookAssetKind.Scene).ToList();
        var generatedSceneScope = scope with { Inputs = [.. scope.Inputs.Where(static input => input.Kind == ContentCookAssetKind.Scene), .. descriptorInputs] };
        var prepared = await PrepareSceneDescriptorsAsync(operationId, generatedSceneScope, descriptors, cancellationToken)
            .ConfigureAwait(false);
        if (HasError(prepared.Diagnostics))
        {
            return new ContentCookResult(
                operationId,
                scope.TargetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, prepared.Diagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var manifest = this.manifestBuilder.BuildSceneManifests(prepared.Scope, prepared.SceneDescriptors);
        return await this.ExecuteManifestAsync(operationId, scope.TargetKind, prepared.Scope, manifest, prepared.Diagnostics, cancellationToken)
            .ConfigureAwait(false);
    }

    private async Task<List<SceneDescriptorGenerationResult>> GenerateSceneDescriptorsAsync(
        ContentCookScope scope, List<ContentCookInput> sceneInputs, CancellationToken cancellationToken)
    {
        var project = CreateProject(scope.Project);
        var descriptors = new List<SceneDescriptorGenerationResult>();
        foreach (var input in sceneInputs)
        {
            var bytes = await File.ReadAllBytesAsync(input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
            var stream = new MemoryStream(bytes, writable: false);
            await using var lifetime = stream.ConfigureAwait(false);
            var scene = await new SceneSerializer(project).DeserializeAsync(stream).ConfigureAwait(false);
            var singleSceneScope = scope with { Inputs = [input] };
            var descriptor = await this.sceneDescriptorGenerator.GenerateAsync(scene, singleSceneScope, cancellationToken).ConfigureAwait(false);
            descriptors.Add(descriptor);
        }

        return descriptors;
    }

    private async Task<ContentCookResult> CookResolvedInputsAsync(
        Guid operationId,
        ContentCookScope scope,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        var allDiagnostics = diagnostics
            .Concat(CreateSourceMissingDiagnostics(operationId, scope.Inputs))
            .ToList();
        if (HasError(allDiagnostics))
        {
            return new ContentCookResult(
                operationId,
                scope.TargetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, allDiagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var prepared = await PrepareScopeInputsAsync(operationId, scope, allDiagnostics, cancellationToken)
            .ConfigureAwait(false);
        if (HasError(prepared.Diagnostics))
        {
            return new ContentCookResult(
                operationId,
                scope.TargetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, prepared.Diagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var manifest = this.manifestBuilder.BuildManifest(prepared.Scope);
        return await this.ExecuteManifestAsync(operationId, scope.TargetKind, prepared.Scope, manifest, prepared.Diagnostics, cancellationToken)
            .ConfigureAwait(false);
    }

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
        manifest = manifest with { Jobs = keptJobs.Select(job => job with { DependsOn = job.DependsOn.Where(keptIds.Contains).ToArray() }).ToArray() };
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
        return !importResult.Succeeded
            ? new ContentCookResult(
                operationId,
                targetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, allDiagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null)
            : await this.ValidateImportedOutputAsync(operationId, targetKind, scope, manifest, allDiagnostics, importResult.OutputFiles, cancellationToken).ConfigureAwait(false);
    }

    private async Task<ContentCookResult> ValidateImportedOutputAsync(Guid operationId, CookTargetKind targetKind, ContentCookScope scope, ContentImportManifest manifest, List<DiagnosticRecord> allDiagnostics, IReadOnlyList<string>? outputFiles, CancellationToken cancellationToken)
    {
        var outputLease = await CookOutputReadLease.AcquireAsync(manifest.Output, cancellationToken).ConfigureAwait(false);
        await using var outputLifetime = outputLease.ConfigureAwait(false);
        CookRunContext.Report(new(Message: "Inspecting cooked output.", State: CookRunState.Validating));
        var inspection = await this.engineContentPipelineApi.InspectLooseCookedRootAsync(manifest.Output, cancellationToken)
            .ConfigureAwait(false);
        allDiagnostics.AddRange(inspection.Diagnostics);
        if (!inspection.Succeeded)
        {
            return new ContentCookResult(
                operationId,
                targetKind,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, allDiagnostics),
                CookedAssets: [],
                inspection,
                Validation: null);
        }

        CookRunContext.Report(new(Message: "Validating cooked output."));
        var validation = await this.engineContentPipelineApi.ValidateLooseCookedRootAsync(manifest.Output, cancellationToken)
            .ConfigureAwait(false);
        allDiagnostics.AddRange(validation.Diagnostics);

        var proof = validation.Succeeded && outputLease.HasIndex
            ? await outputLease.CaptureAsync(scope.Inputs[0].MountName, inspection, cancellationToken).ConfigureAwait(false)
            : null;
        return new ContentCookResult(
            operationId,
            targetKind,
            GetStatus(allDiagnostics, validation),
            NormalizeDiagnostics(operationId, allDiagnostics),
            CreateCookedAssets(scope, inspection, outputFiles),
            inspection,
            validation) { VerifiedRoot = proof };
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
            manifest) { Artifacts = scope.Artifacts };
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

    private sealed record PreparedInput(ContentCookInput Input, IReadOnlyList<DiagnosticRecord> Diagnostics);

    private sealed record PreparedScope(ContentCookScope Scope, IReadOnlyList<DiagnosticRecord> Diagnostics);

    private sealed record PreparedSceneDescriptors(
        ContentCookScope Scope,
        IReadOnlyList<SceneDescriptorGenerationResult> SceneDescriptors,
        IReadOnlyList<DiagnosticRecord> Diagnostics);
}
