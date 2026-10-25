// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

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
        if (scope.Inputs.Any(input => !string.Equals(input.MountName, mountName, StringComparison.Ordinal)))
        {
            throw new InvalidOperationException("One content import manifest cannot span multiple authoring mounts.");
        }

        var output = scope.Output ?? throw new InvalidOperationException("A manifest requires an owned candidate generation.");
        var jobs = this.CreateDependencyJobs(scope, scope.Inputs);

        return new ContentImportManifest(
            Version: 1,
            Output: output.Path,
            Layout: new ContentImportLayout(AssetPath.Create(mountName).VirtualPath),
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

        // A generated input, such as the default material, can be staged both by the scene's analysis and by the
        // cook's builtins; it is one input. The scope's copy wins, as in the cook's own input list.
        var inputs = scope.Inputs.Where(static input => input.Kind != ContentCookAssetKind.Scene)
            .Concat(sceneDescriptors.SelectMany(static descriptor => descriptor.Dependencies))
            .DistinctBy(static input => input.AssetUri)
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
                FileSystemPath.From(descriptor.DescriptorPath),
                ContentCookInputRole.GeneratedDescriptor);
            var sceneJob = scope.NativeJobs.GetValueOrDefault(descriptor.SceneAssetUri) ?? this.BuildJob(sceneDescriptorInput, dependencyIds);
            jobs.Add(sceneJob with { DependsOn = dependencyIds });
            if (descriptor.HasScriptSidecar)
            {
                jobs.Add(BuildScriptSidecarJob(sceneJob, descriptor, jobIdsBySource));
            }
        }

        return new ContentImportManifest(
            Version: 1,
            Output: output.Path,
            Layout: new ContentImportLayout(AssetPath.Create(mountName).VirtualPath),
            Jobs: jobs)
        { SourceKey = output.SourceKey };
    }

    /// <inheritdoc />
    public ContentImportJob BuildJob(ContentCookInput input, IReadOnlyList<string> dependsOn, string? modelRecordPath = null)
    {
        ArgumentNullException.ThrowIfNull(input);
        ArgumentNullException.ThrowIfNull(dependsOn);
        if (input.Kind == ContentCookAssetKind.ForeignSource)
        {
            ArgumentException.ThrowIfNullOrEmpty(modelRecordPath);

            // The record holds the model's source, identity and settings; the job only schedules it.
            return new(
                BuildJobId(input),
                "model",
                NormalizeSource(input.SourceRelativePath),
                dependsOn,
                Output: null,
                Name: null)
            {
                Record = NormalizeSource(modelRecordPath),
                Layout = new ContentImportLayout(AssetPath.Create(input.MountName).VirtualPath),
            };
        }

        var job = new ContentImportJob(
            BuildJobId(input),
            GetJobType(input.Kind),
            NormalizeSource(input.SourceRelativePath),
            dependsOn,
            Output: null,
            Name: input.Kind == ContentCookAssetKind.Input ? null : Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(input.SourceRelativePath)))
        {
            Layout = new ContentImportLayout(AssetPath.Create(input.MountName).VirtualPath),

            // A source that cooks one asset names it, and the cooker places its descriptor there; an input source
            // names itself, and the cooker names its actions and contexts under it.
            VirtualPath = input.AssetUri.VirtualPath,
        };

        // Scripts cook to debug bytecode that records their source, so the player runs the bytecode and reloads
        // the source when a script source root has it.
        return input.Kind == ContentCookAssetKind.Script
            ? job with
            {
                Compile = true,
                CompileMode = "debug",
                ScriptStorage = "external",
                SourceRoot = GetScriptSourceRoot(input),
            }
            : job;
    }

    /// <inheritdoc />
    public ContentImportJob BuildModelJob(string sourceRelativePath, IReadOnlyList<string> dependsOn, string name, AssetPath identity)
        => new(
            BuildJobId(ContentCookAssetKind.ForeignSource, sourceRelativePath),
            Path.GetExtension(sourceRelativePath).Equals(".fbx", StringComparison.OrdinalIgnoreCase) ? "fbx" : "gltf",
            NormalizeSource(sourceRelativePath),
            dependsOn,
            Output: null,
            name)
        {
            // The model names its products under its identity, the virtual path of its source file.
            Layout = new ContentImportLayout(AssetPath.Create(identity.Mount).VirtualPath),
            VirtualPath = identity.VirtualPath,
        };

    /// <summary>Builds the job that attaches a scene's script slots to its cooked scene.</summary>
    /// <param name="sceneJob">The scene's job, which the sidecar patches.</param>
    /// <param name="descriptor">The scene's projection, with its bindings.</param>
    /// <param name="jobIdsBySource">The job of each input in the manifest.</param>
    /// <returns>The sidecar job; it runs after the scene and the scripts it uses.</returns>
    private static ContentImportJob BuildScriptSidecarJob(
        ContentImportJob sceneJob,
        SceneDescriptorGenerationResult descriptor,
        Dictionary<AssetPath, string> jobIdsBySource)
        => new(
            "script-sidecar:" + NormalizeSource(sceneJob.AttributedSource),
            "script-sidecar",
            Source: null,
            [sceneJob.Id, .. descriptor.ScriptReferences.Where(jobIdsBySource.ContainsKey).Select(uri => jobIdsBySource[uri]).Distinct(StringComparer.Ordinal)],
            Output: null,
            Name: null)
        {
            TargetSceneVirtualPath = descriptor.DescriptorVirtualPath,
            Bindings = descriptor.ScriptBindings,
            InstanceBindings = descriptor.ScriptInstanceBindings.Count == 0 ? null : descriptor.ScriptInstanceBindings,
            OwnerSource = sceneJob.Source,
        };

    private static string GetSingleMountName(ContentCookScope scope)
    {
        if (scope.Inputs.Count == 0)
        {
            throw new InvalidOperationException("Content import manifest requires at least one input.");
        }

        var primaryInput = scope.Inputs[0];
        var mountName = primaryInput.MountName;
        return scope.Inputs.Any(input => !string.Equals(input.MountName, mountName, StringComparison.Ordinal))
            ? throw new InvalidOperationException("One content import manifest cannot span multiple authoring mounts.")
            : mountName;
    }

    // The authoring mount's folder: the player resolves a script's recorded source path against the project's
    // authoring roots, so the path is recorded relative to the mount.
    private static string GetScriptSourceRoot(ContentCookInput input)
    {
        var depth = input.AssetUri.RelativePath.Count(static character => character == '/');
        var root = input.SourceAbsolutePath.Parent!.FullPath;
        for (var level = 0; level < depth; level++)
        {
            root = Path.GetDirectoryName(root)!;
        }

        return root.Replace('\\', '/');
    }

    private static string GetJobType(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Material => "material-descriptor",
            ContentCookAssetKind.Geometry => "geometry-descriptor",
            ContentCookAssetKind.Scene => "scene-descriptor",
            ContentCookAssetKind.Texture => "texture-descriptor",
            ContentCookAssetKind.Input => "input",
            ContentCookAssetKind.Script => "script",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

    private static string BuildJobId(ContentCookInput input) => BuildJobId(input.Kind, input.SourceRelativePath);

    private static string BuildJobId(ContentCookAssetKind kind, string sourceRelativePath)
        => GetJobKindPrefix(kind) + ":" + NormalizeSource(sourceRelativePath);

    private static string GetJobKindPrefix(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Material => "material",
            ContentCookAssetKind.Geometry => "geometry",
            ContentCookAssetKind.Scene => "scene",
            ContentCookAssetKind.Texture => "texture",
            ContentCookAssetKind.ForeignSource => "model",
            ContentCookAssetKind.Input => "input",
            ContentCookAssetKind.Script => "script",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

    private static string ToProjectRelativePath(string projectRoot, string absolutePath)
        => NormalizeSource(Path.GetRelativePath(projectRoot, absolutePath));

    private static string NormalizeSource(string source)
        => source.Replace('\\', '/').TrimStart('/');

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
}
