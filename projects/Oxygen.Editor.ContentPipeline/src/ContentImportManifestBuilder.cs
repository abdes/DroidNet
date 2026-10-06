// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Builds native import manifests for explicit editor cook workflows.
/// </summary>
public sealed class ContentImportManifestBuilder : IContentImportManifestBuilder
{
    /// <inheritdoc />
    public ContentImportManifest BuildManifest(ContentCookScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        if (scope.Inputs.Count == 0)
        {
            throw new InvalidOperationException("Content import manifest requires at least one input.");
        }

        var primaryInput = scope.Inputs[0];
        var mountName = primaryInput.MountName;
        if (scope.Inputs.Any(input => !string.Equals(input.MountName, mountName, StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidOperationException("One content import manifest cannot span multiple authoring mounts.");
        }

        var output = scope.Output ?? throw new InvalidOperationException("A manifest requires an owned candidate generation.");
        var jobs = this.CreateDependencyJobs(scope, scope.Inputs);

        return new ContentImportManifest(
            Version: 1,
            Output: output.Path,
            Layout: new ContentImportLayout(ContentPipelinePaths.GetVirtualMountRoot(mountName)),
            Jobs: jobs)
        { SourceKey = output.SourceKey };
    }

    /// <inheritdoc />
    public ContentImportManifest BuildSceneManifest(
        ContentCookScope scope,
        SceneDescriptorGenerationResult sceneDescriptor)
        => this.BuildSceneManifests(scope, [sceneDescriptor]);

    /// <inheritdoc />
    public ContentImportManifest BuildSceneManifests(
        ContentCookScope scope,
        IReadOnlyList<SceneDescriptorGenerationResult> sceneDescriptors)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(sceneDescriptors);

        if (sceneDescriptors.Count == 0)
        {
            throw new InvalidOperationException("Scene import manifest requires at least one generated scene descriptor.");
        }

        var mountName = GetSingleMountName(scope);
        var output = scope.Output ?? throw new InvalidOperationException("A manifest requires an owned candidate generation.");
        var inputs = sceneDescriptors.SelectMany(static descriptor => descriptor.Dependencies)
            .Concat(scope.Inputs.Where(static input => input.Kind != ContentCookAssetKind.Scene))
            .DistinctBy(static input => input.SourceRelativePath, StringComparer.Ordinal).ToArray();
        var jobs = this.CreateDependencyJobs(scope, inputs);
        var jobIdsBySource = inputs.ToDictionary(
            static input => input.AssetUri,
            input => scope.NativeJobs.TryGetValue(input.AssetUri, out var job) ? job.Id : BuildJobId(input));

        foreach (var descriptor in sceneDescriptors.OrderBy(static item => item.DescriptorPath, StringComparer.Ordinal))
        {
            var dependencyIds = descriptor.Dependencies
                .Select(static dependency => dependency.AssetUri)
                .Concat(scope.InputDependencies.GetValueOrDefault(descriptor.SceneAssetUri, []))
                .Where(jobIdsBySource.ContainsKey)
                .Select(uri => jobIdsBySource[uri])
                .Distinct(StringComparer.Ordinal)
                .ToList();
            var sceneDescriptorInput = new ContentCookInput(
                descriptor.SceneAssetUri,
                ContentCookAssetKind.Scene,
                mountName,
                ToProjectRelativePath(scope.InputRoot, descriptor.DescriptorPath),
                descriptor.DescriptorPath,
                descriptor.DescriptorVirtualPath,
                ContentCookInputRole.GeneratedDescriptor);
            var sceneJob = scope.NativeJobs.GetValueOrDefault(descriptor.SceneAssetUri) ?? this.BuildJob(sceneDescriptorInput, dependencyIds);
            jobs.Add(sceneJob with { DependsOn = dependencyIds });
        }

        return new ContentImportManifest(
            Version: 1,
            Output: output.Path,
            Layout: new ContentImportLayout(ContentPipelinePaths.GetVirtualMountRoot(mountName)),
            Jobs: jobs)
        { SourceKey = output.SourceKey };
    }

    /// <inheritdoc />
    public ContentImportJob BuildJob(ContentCookInput input, IReadOnlyList<string> dependsOn, Import.NativeSceneImportSettings? modelSettings = null)
    {
        ArgumentNullException.ThrowIfNull(input);
        ArgumentNullException.ThrowIfNull(dependsOn);
        if (input.Kind == ContentCookAssetKind.ForeignSource)
        {
            ArgumentNullException.ThrowIfNull(modelSettings);
            return this.BuildModelJob(input, dependsOn, modelSettings.Name, modelSettings.CreateLayout(), modelSettings.MaterialSlotProvenance) with
            {
                ContentPolicy = modelSettings.ContentPolicy,
                UnitPolicy = modelSettings.UnitPolicy,
                BakeTransforms = modelSettings.BakeTransforms,
                NormalsPolicy = modelSettings.NormalsPolicy,
                TangentsPolicy = modelSettings.TangentsPolicy,
            };
        }

        return new(
            BuildJobId(input),
            GetJobType(input.Kind),
            NormalizeSource(input.SourceRelativePath),
            dependsOn,
            Output: null,
            Name: Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(input.SourceRelativePath)))
        {
            Layout = CreateJobLayout(input),
            VirtualPath = input.Kind == ContentCookAssetKind.Texture ? input.OutputVirtualPath : null,
        };
    }

    /// <inheritdoc />
    public ContentImportJob BuildModelJob(ContentCookInput input, IReadOnlyList<string> dependsOn, string name, ContentImportLayout layout, Import.NativeMaterialSlotProvenance provenance)
        => new(
            BuildJobId(input),
            Path.GetExtension(input.SourceRelativePath).Equals(".fbx", StringComparison.OrdinalIgnoreCase) ? "fbx" : "gltf",
            NormalizeSource(input.SourceRelativePath),
            dependsOn,
            Output: null,
            name)
        {
            Layout = layout,
            ContentPolicy = Import.NativeSceneImportSettings.DefaultContentPolicy,
            UnitPolicy = Import.NativeSceneImportSettings.DefaultUnitPolicy,
            BakeTransforms = false,
            NormalsPolicy = Import.NativeSceneImportSettings.DefaultNormalsPolicy,
            TangentsPolicy = Import.NativeSceneImportSettings.DefaultTangentsPolicy,
            MaterialSlotSourceIdentity = provenance.SourceIdentity.ToString("D"),
            MaterialSlotProvenance = provenance,
        };

    private List<ContentImportJob> CreateDependencyJobs(ContentCookScope scope, IReadOnlyList<ContentCookInput> inputs)
    {
        var jobs = new List<ContentImportJob>();
        var jobIds = inputs.ToDictionary(
            static input => input.AssetUri,
            input => scope.NativeJobs.TryGetValue(input.AssetUri, out var job) ? job.Id : BuildJobId(input));
        foreach (var input in inputs
                     .OrderBy(static item => item.Kind)
                     .ThenBy(static item => item.SourceRelativePath, StringComparer.Ordinal))
        {
            var dependencies = scope.InputDependencies.TryGetValue(input.AssetUri, out var declared)
                ? declared.Where(jobIds.ContainsKey).Select(uri => jobIds[uri]).Distinct(StringComparer.Ordinal).ToArray() : [];
            var job = (scope.NativeJobs.GetValueOrDefault(input.AssetUri) ?? this.BuildJob(input, dependencies)) with { DependsOn = dependencies };
            jobs.Add(job);
        }

        return jobs;
    }

    private static string GetSingleMountName(ContentCookScope scope)
    {
        if (scope.Inputs.Count == 0)
        {
            throw new InvalidOperationException("Content import manifest requires at least one input.");
        }

        var primaryInput = scope.Inputs[0];
        var mountName = primaryInput.MountName;
        return scope.Inputs.Any(input => !string.Equals(input.MountName, mountName, StringComparison.OrdinalIgnoreCase))
            ? throw new InvalidOperationException("One content import manifest cannot span multiple authoring mounts.")
            : mountName;
    }

    private static ContentImportLayout? CreateJobLayout(ContentCookInput input)
    {
        if (input.OutputVirtualPath is not { } output)
        {
            return null;
        }

        var mount = ContentPipelinePaths.GetVirtualMountRoot(input.MountName);
        if (!output.StartsWith(mount + "/", StringComparison.Ordinal))
        {
            throw new ArgumentException("The descriptor output must remain within its authoring mount.", nameof(input));
        }

        var relative = output[(mount.Length + 1)..];
        var folder = Path.GetDirectoryName(relative)?.Replace('\\', '/') ?? string.Empty;
        var layout = new ContentImportLayout(mount) { DescriptorsDirectory = string.Empty };
        return input.Kind switch
        {
            ContentCookAssetKind.Material => layout with { MaterialsDirectory = folder },
            ContentCookAssetKind.Geometry => layout with { GeometryDirectory = folder },
            ContentCookAssetKind.Scene => layout with { ScenesDirectory = folder },
            ContentCookAssetKind.Texture => layout,
            _ => null,
        };
    }

    private static string GetJobType(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Material => "material-descriptor",
            ContentCookAssetKind.Geometry => "geometry-descriptor",
            ContentCookAssetKind.Scene => "scene-descriptor",
            ContentCookAssetKind.Texture => "texture-descriptor",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

    private static string BuildJobId(ContentCookInput input)
        => GetJobKindPrefix(input.Kind) + ":" + NormalizeSource(input.SourceRelativePath);

    private static string GetJobKindPrefix(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Material => "material",
            ContentCookAssetKind.Geometry => "geometry",
            ContentCookAssetKind.Scene => "scene",
            ContentCookAssetKind.Texture => "texture",
            ContentCookAssetKind.ForeignSource => "model",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

    private static string ToProjectRelativePath(string projectRoot, string absolutePath)
        => NormalizeSource(Path.GetRelativePath(projectRoot, absolutePath));

    private static string NormalizeSource(string source)
        => source.Replace('\\', '/').TrimStart('/');
}
