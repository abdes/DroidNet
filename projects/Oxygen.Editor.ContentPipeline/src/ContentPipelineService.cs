// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Import.Materials;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Default explicit editor content-pipeline workflow service.
/// </summary>
public sealed class ContentPipelineService : IContentPipelineService
{
    private static readonly System.Text.Json.JsonSerializerOptions NativeDescriptorJsonOptions = new()
    {
        WriteIndented = true,
    };

    private readonly IProjectContextService projectContextService;
    private readonly IProjectCookScopeProvider cookScopeProvider;
    private readonly ISceneDescriptorGenerator sceneDescriptorGenerator;
    private readonly IContentImportManifestBuilder manifestBuilder;
    private readonly IContentImportManifestValidator manifestValidator;
    private readonly IEngineContentPipelineApi engineContentPipelineApi;

    /// <summary>
    /// Initializes a new instance of the <see cref="ContentPipelineService"/> class.
    /// </summary>
    /// <param name="projectContextService">The active project context service.</param>
    /// <param name="cookScopeProvider">The project cook scope provider.</param>
    /// <param name="sceneDescriptorGenerator">The scene descriptor generator.</param>
    /// <param name="manifestBuilder">The import manifest builder.</param>
    /// <param name="manifestValidator">The import manifest validator.</param>
    /// <param name="engineContentPipelineApi">The engine content-pipeline adapter.</param>
    public ContentPipelineService(
        IProjectContextService projectContextService,
        IProjectCookScopeProvider cookScopeProvider,
        ISceneDescriptorGenerator sceneDescriptorGenerator,
        IContentImportManifestBuilder manifestBuilder,
        IContentImportManifestValidator manifestValidator,
        IEngineContentPipelineApi engineContentPipelineApi)
    {
        this.projectContextService = projectContextService ?? throw new ArgumentNullException(nameof(projectContextService));
        this.cookScopeProvider = cookScopeProvider ?? throw new ArgumentNullException(nameof(cookScopeProvider));
        this.sceneDescriptorGenerator = sceneDescriptorGenerator ?? throw new ArgumentNullException(nameof(sceneDescriptorGenerator));
        this.manifestBuilder = manifestBuilder ?? throw new ArgumentNullException(nameof(manifestBuilder));
        this.manifestValidator = manifestValidator ?? throw new ArgumentNullException(nameof(manifestValidator));
        this.engineContentPipelineApi = engineContentPipelineApi ?? throw new ArgumentNullException(nameof(engineContentPipelineApi));
    }

    /// <inheritdoc />
    public async Task<ContentCookResult> CookCurrentSceneAsync(
        Scene scene,
        Uri sceneAssetUri,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(sceneAssetUri);
        cancellationToken.ThrowIfCancellationRequested();

        var operationId = Guid.NewGuid();
        var project = this.RequireActiveProject();
        var scope = this.CreateSceneScope(project, sceneAssetUri);
        var descriptor = await this.sceneDescriptorGenerator.GenerateAsync(scene, scope, cancellationToken)
            .ConfigureAwait(false);
        if (HasError(descriptor.Diagnostics))
        {
            return new ContentCookResult(
                operationId,
                CookTargetKind.CurrentScene,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, descriptor.Diagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var dependencyDiagnostics = CreateSourceMissingDiagnostics(operationId, descriptor.Dependencies);
        if (dependencyDiagnostics.Count > 0)
        {
            return new ContentCookResult(
                operationId,
                CookTargetKind.CurrentScene,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, dependencyDiagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var prepared = await this.PrepareSceneDescriptorsAsync(operationId, scope, [descriptor], cancellationToken)
            .ConfigureAwait(false);
        if (HasError(prepared.Diagnostics))
        {
            return new ContentCookResult(
                operationId,
                CookTargetKind.CurrentScene,
                OperationStatus.Failed,
                NormalizeDiagnostics(operationId, prepared.Diagnostics),
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var manifest = this.manifestBuilder.BuildSceneManifest(prepared.Scope, prepared.SceneDescriptors[0]);
        return await this.ExecuteManifestAsync(
                operationId,
                CookTargetKind.CurrentScene,
                prepared.Scope,
                manifest,
                prepared.Diagnostics,
                cancellationToken)
            .ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task<ContentCookResult> CookAssetAsync(Uri assetUri, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        cancellationToken.ThrowIfCancellationRequested();

        var operationId = Guid.NewGuid();
        var project = this.RequireActiveProject();
        var input = ResolveInput(project, assetUri, GetAssetKind(assetUri), ContentCookInputRole.Primary);
        var scope = this.CreateScope(project, [input], CookTargetKind.Asset);
        return input.Kind == ContentCookAssetKind.Scene
            ? await this.CookSceneInputsAsync(operationId, scope, cancellationToken).ConfigureAwait(false)
            : await this.CookResolvedInputsAsync(operationId, scope, diagnostics: [], cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task<ContentCookResult> CookFolderAsync(Uri folderUri, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(folderUri);
        cancellationToken.ThrowIfCancellationRequested();

        var operationId = Guid.NewGuid();
        var project = this.RequireActiveProject();
        var scope = this.CreateScope(project, ResolveFolderInputs(project, folderUri), CookTargetKind.Folder);
        return await this.CookMixedInputsAsync(operationId, scope, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task<ContentCookResult> CookProjectAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var operationId = Guid.NewGuid();
        var project = this.RequireActiveProject();
        var scopes = project.AuthoringMounts
            .Where(static mount => !IsDerivedRootMount(mount))
            .Select(mount => this.CreateScope(project, ResolveMountInputs(project, mount), CookTargetKind.Project))
            .Where(static scope => scope.Inputs.Count > 0)
            .ToList();
        if (scopes.Count == 0)
        {
            return new ContentCookResult(
                operationId,
                CookTargetKind.Project,
                OperationStatus.Succeeded,
                Diagnostics: [],
                CookedAssets: [],
                Inspection: null,
                Validation: null);
        }

        var results = new List<ContentCookResult>();
        foreach (var scope in scopes)
        {
            results.Add(await this.CookMixedInputsAsync(operationId, scope, cancellationToken).ConfigureAwait(false));
        }

        return MergeProjectResults(operationId, results);
    }

    /// <inheritdoc />
    public Task<CookInspectionResult> InspectCookedOutputAsync(Uri? scopeUri, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var cookedRoot = this.ResolveCookedRoot(scopeUri);
        return this.engineContentPipelineApi.InspectLooseCookedRootAsync(cookedRoot, cancellationToken);
    }

    /// <inheritdoc />
    public Task<CookValidationResult> ValidateCookedOutputAsync(Uri? scopeUri, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var cookedRoot = this.ResolveCookedRoot(scopeUri);
        return this.engineContentPipelineApi.ValidateLooseCookedRootAsync(cookedRoot, cancellationToken);
    }

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
        if (statusList.Any(static status => status == OperationStatus.Failed))
        {
            return statusList.Any(static status => status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings)
                ? OperationStatus.PartiallySucceeded
                : OperationStatus.Failed;
        }

        return statusList.Any(static status => status == OperationStatus.SucceededWithWarnings)
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

    private static IReadOnlyList<DiagnosticRecord> NormalizeDiagnostics(
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
    {
        if (!validation.Succeeded)
        {
            return OperationStatus.Failed;
        }

        return descriptorDiagnostics.Any(static diagnostic => diagnostic.Severity == DiagnosticSeverity.Warning)
            ? OperationStatus.SucceededWithWarnings
            : OperationStatus.Succeeded;
    }

    private static List<ContentCookedAsset> CreateCookedAssets(
        ContentCookScope scope,
        CookInspectionResult inspection)
    {
        var inputsByVirtualPath = scope.Inputs
            .Where(static input => !string.IsNullOrWhiteSpace(input.OutputVirtualPath))
            .ToDictionary(static input => input.OutputVirtualPath!, StringComparer.Ordinal);
        return inspection.Assets
            .Select(asset =>
            {
                var cookedUri = ToAssetUri(asset.VirtualPath);
                var input = inputsByVirtualPath.GetValueOrDefault(asset.VirtualPath);
                return new ContentCookedAsset(
                    input?.AssetUri ?? cookedUri,
                    cookedUri,
                    asset.Kind,
                    GetMountName(asset.VirtualPath),
                    asset.VirtualPath);
            })
            .ToList();
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
        if (path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
            || path.EndsWith(".omat", StringComparison.OrdinalIgnoreCase))
        {
            return ContentCookAssetKind.Material;
        }

        if (path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
            || path.EndsWith(".ogeo", StringComparison.OrdinalIgnoreCase))
        {
            return ContentCookAssetKind.Geometry;
        }

        if (path.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase)
            || path.EndsWith(".oscene", StringComparison.OrdinalIgnoreCase))
        {
            return ContentCookAssetKind.Scene;
        }

        throw new ArgumentException($"Unsupported content cook asset URI '{assetUri}'.", nameof(assetUri));
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
        if (!string.Equals(assetUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("Cook input must be an editor asset URI.", nameof(assetUri));
        }

        var path = Uri.UnescapeDataString(assetUri.AbsolutePath).TrimStart('/').Replace('\\', '/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            throw new ArgumentException("Cook input URI must include a mount name and path.", nameof(assetUri));
        }

        var mountName = path[..slash];
        var mountRelativePath = path[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase))
                    ?? throw new InvalidOperationException($"Project does not declare authoring mount '{mountName}'.");
        if (IsDerivedRootMount(mount))
        {
            throw new InvalidOperationException($"Project mount '{mountName}' is a derived output root and cannot be used as cook input.");
        }

        var sourceRelativePath = Path.Combine(mount.RelativePath, mountRelativePath).Replace('\\', '/');
        var sourceAbsolutePath = Path.GetFullPath(Path.Combine(project.ProjectRoot, sourceRelativePath));
        return new ContentCookInput(
            assetUri,
            kind,
            mountName,
            sourceRelativePath,
            sourceAbsolutePath,
            ContentPipelinePaths.ToNativeDescriptorPath(assetUri, GetExpectedExtension(kind)),
            role);
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
           || path.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase);

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
        if (!Directory.Exists(absoluteFolder))
        {
            return [];
        }

        return Directory.EnumerateFiles(absoluteFolder, "*.json", SearchOption.AllDirectories)
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
        if (!Directory.Exists(mountRoot))
        {
            return [];
        }

        return Directory.EnumerateFiles(mountRoot, "*.json", SearchOption.AllDirectories)
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
        return new ContentCookInput(
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

    private static string? GetDefaultCookedMountName(ProjectContext project)
        => project.AuthoringMounts.FirstOrDefault(static mount => !IsDerivedRootMount(mount))?.Name;

    private static string? GetCookedMountName(ProjectContext project, string virtualPath)
    {
        var normalized = virtualPath.Trim('/').Replace('\\', '/');
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return GetDefaultCookedMountName(project);
        }

        var slash = normalized.IndexOf('/', StringComparison.Ordinal);
        var root = slash <= 0 ? normalized : normalized[..slash];
        if (!string.Equals(root, "Cooked", StringComparison.OrdinalIgnoreCase))
        {
            return root;
        }

        if (slash <= 0 || slash == normalized.Length - 1)
        {
            return GetDefaultCookedMountName(project);
        }

        var remaining = normalized[(slash + 1)..];
        var nextSlash = remaining.IndexOf('/', StringComparison.Ordinal);
        return nextSlash <= 0 ? remaining : remaining[..nextSlash];
    }

    private ProjectContext RequireActiveProject()
        => this.projectContextService.ActiveProject
           ?? throw new InvalidOperationException("Content pipeline requires an active project.");

    private async Task<ContentCookResult> CookMixedInputsAsync(
        Guid operationId,
        ContentCookScope scope,
        CancellationToken cancellationToken)
    {
        if (scope.Inputs.Any(static input => input.Kind == ContentCookAssetKind.Scene))
        {
            return await this.CookSceneInputsAsync(operationId, scope, cancellationToken).ConfigureAwait(false);
        }

        return await this.CookResolvedInputsAsync(operationId, scope, diagnostics: [], cancellationToken).ConfigureAwait(false);
    }

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

        var project = CreateProject(scope.Project);
        var descriptors = new List<SceneDescriptorGenerationResult>();
        foreach (var input in sceneInputs)
        {
            using var stream = File.OpenRead(input.SourceAbsolutePath);
            var scene = await new SceneSerializer(project).DeserializeAsync(stream).ConfigureAwait(false);
            var singleSceneScope = this.CreateScope(scope.Project, [input], scope.TargetKind);
            var descriptor = await this.sceneDescriptorGenerator.GenerateAsync(scene, singleSceneScope, cancellationToken)
                .ConfigureAwait(false);
            descriptors.Add(descriptor);
        }

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
        var prepared = await this.PrepareSceneDescriptorsAsync(operationId, generatedSceneScope, descriptors, cancellationToken)
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

    private async Task<PreparedSceneDescriptors> PrepareSceneDescriptorsAsync(
        Guid operationId,
        ContentCookScope scope,
        IReadOnlyList<SceneDescriptorGenerationResult> descriptors,
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

        return new PreparedSceneDescriptors(preparedScope.Scope, preparedDescriptors, diagnostics);
    }

    private static async Task<PreparedInput> PrepareInputAsync(
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
            var generatedAbsolutePath = GetGeneratedMaterialDescriptorPath(scope.Project.ProjectRoot, input);
            Directory.CreateDirectory(Path.GetDirectoryName(generatedAbsolutePath)!);

            var materialBytes = await File.ReadAllBytesAsync(input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
            var material = MaterialSourceReader.Read(materialBytes);
            var native = ToNativeMaterialDescriptor(input, material);
            await using (var stream = File.Create(generatedAbsolutePath))
            {
                await System.Text.Json.JsonSerializer.SerializeAsync(
                        stream,
                        native,
                        NativeDescriptorJsonOptions,
                        cancellationToken)
                    .ConfigureAwait(false);
            }

            var generatedRelativePath = Path.GetRelativePath(scope.Project.ProjectRoot, generatedAbsolutePath)
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

    private async Task<ContentCookResult> ExecuteManifestAsync(
        Guid operationId,
        CookTargetKind targetKind,
        ContentCookScope scope,
        ContentImportManifest manifest,
        IReadOnlyList<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
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

        var importResult = await this.engineContentPipelineApi.ImportAsync(manifest, cancellationToken)
            .ConfigureAwait(false);
        var allDiagnostics = diagnostics.Concat(importResult.Diagnostics).ToList();
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

        var validation = await this.engineContentPipelineApi.ValidateLooseCookedRootAsync(manifest.Output, cancellationToken)
            .ConfigureAwait(false);
        allDiagnostics.AddRange(validation.Diagnostics);

        return new ContentCookResult(
            operationId,
            targetKind,
            GetStatus(allDiagnostics, validation),
            NormalizeDiagnostics(operationId, allDiagnostics),
            CreateCookedAssets(scope, inspection),
            inspection,
            validation);
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

    private string ResolveCookedRoot(Uri? scopeUri)
    {
        var project = this.RequireActiveProject();
        var mountName = scopeUri is null
            ? GetDefaultCookedMountName(project)
            : GetCookedMountName(project, Uri.UnescapeDataString(scopeUri.AbsolutePath));
        if (string.IsNullOrWhiteSpace(mountName))
        {
            throw new InvalidOperationException("Content pipeline requires at least one authoring mount.");
        }

        return ContentPipelinePaths.GetCookedMountRoot(project.ProjectRoot, mountName);
    }

    private sealed record PreparedInput(ContentCookInput Input, IReadOnlyList<DiagnosticRecord> Diagnostics);

    private sealed record PreparedScope(ContentCookScope Scope, IReadOnlyList<DiagnosticRecord> Diagnostics);

    private sealed record PreparedSceneDescriptors(
        ContentCookScope Scope,
        IReadOnlyList<SceneDescriptorGenerationResult> SceneDescriptors,
        IReadOnlyList<DiagnosticRecord> Diagnostics);
}
