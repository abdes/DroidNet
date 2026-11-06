// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
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
            Version: ContentImportManifest.CurrentSchemaVersion,
            Output: output.Path,
            Jobs: jobs)
        { SourceKey = output.SourceKey, Mounts = CreateMounts(scope.Project) };
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
            .DistinctBy(static input => input.SourceCookPath, StringComparer.Ordinal).ToArray();
        var jobs = this.CreateDependencyJobs(scope, inputs);
        var jobIdsBySource = inputs.ToDictionary(
            static input => input.AssetUri,
            input => scope.NativeJobs.TryGetValue(input.AssetUri, out var job) ? job.Id : BuildJobId(input));

        // A scene that instances another scene of this cook reads its cooked output, so it waits for that scene's job.
        var scenes = CreateSceneInputs(scope, mountName, sceneDescriptors);
        foreach (var (descriptor, input) in scenes)
        {
            jobIdsBySource[descriptor.SceneAssetUri] = scope.NativeJobs.TryGetValue(descriptor.SceneAssetUri, out var job) ? job.Id : BuildJobId(input);
        }

        foreach (var (descriptor, sceneDescriptorInput) in scenes)
        {
            var dependencyIds = descriptor.Dependencies
                .Select(static dependency => dependency.AssetUri)
                .Concat(scope.InputDependencies.GetValueOrDefault(descriptor.SceneAssetUri, []))
                .Where(uri => uri != descriptor.SceneAssetUri && jobIdsBySource.ContainsKey(uri))
                .Select(uri => jobIdsBySource[uri])
                .Distinct(StringComparer.Ordinal)
                .ToList();
            var sceneJob = scope.NativeJobs.GetValueOrDefault(descriptor.SceneAssetUri) ?? this.BuildJob(sceneDescriptorInput, dependencyIds);
            jobs.Add(sceneJob with { DependsOn = dependencyIds });
            if (descriptor.HasScriptSidecar)
            {
                jobs.Add(BuildScriptSidecarJob(sceneJob, descriptor, jobIdsBySource));
            }
        }

        return new ContentImportManifest(
            Version: ContentImportManifest.CurrentSchemaVersion,
            Output: output.Path,
            Jobs: jobs)
        { SourceKey = output.SourceKey, Mounts = CreateMounts(scope.Project) };
    }

    /// <inheritdoc />
    public ContentImportJob BuildJob(ContentCookInput input, IReadOnlyList<string> dependsOn)
    {
        ArgumentNullException.ThrowIfNull(input);
        ArgumentNullException.ThrowIfNull(dependsOn);
        if (input.Kind == ContentCookAssetKind.ForeignSource)
        {
            // The tool reads the record at the model's identity, which holds its source and settings; the job only
            // schedules it. The source stays here, unwritten, for what the job's report is attributed to.
            return new(
                BuildJobId(input),
                "model",
                input.SourceCookPath,
                dependsOn,
                Output: null,
                Name: null)
            {
                VirtualPath = input.AssetUri.VirtualPath,
            };
        }

        var job = new ContentImportJob(
            BuildJobId(input),
            GetJobType(input.Kind),
            input.SourceCookPath,
            dependsOn,
            Output: null,
            Name: input.Kind == ContentCookAssetKind.Input ? null : Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(input.SourceCookPath)))
        {
            // A source that cooks one asset names it, and the cooker places its descriptor there; an input source
            // names itself, and the cooker names its actions and contexts under it. A buffer container's identity is
            // its source's, and its buffers are named from it.
            VirtualPath = input.Kind == ContentCookAssetKind.BufferContainer ? null : input.AssetUri.VirtualPath,
            DerivedFrom = input.DerivedFrom,
        };

        // Scripts cook to debug bytecode that records their source's mount path, so the player runs the bytecode and
        // reloads the source when a launch source mount has it.
        return input.Kind == ContentCookAssetKind.Script
            ? job with
            {
                Compile = true,
                CompileMode = "debug",
                ScriptStorage = "external",
            }
            : job;
    }

    /// <inheritdoc />
    public ContentImportJob BuildModelJob(string source, IReadOnlyList<string> dependsOn, string name, AssetPath identity)
        => new(
            BuildJobId(ContentCookAssetKind.ForeignSource, NormalizeSource(source)),
            Path.GetExtension(source).Equals(".fbx", StringComparison.OrdinalIgnoreCase) ? "fbx" : "gltf",
            NormalizeSource(source),
            dependsOn,
            Output: null,
            name)
        {
            // The model names its products under its identity, the virtual path of its source file.
            VirtualPath = identity.VirtualPath,
        };

    /// <summary>Gets the importers whose output makes up a source's products, by the names the ImportTool reports.</summary>
    /// <param name="input">The cook input.</param>
    /// <returns>The importers its jobs run: a scene's script sidecar patches its cooked scene.</returns>
    internal static IReadOnlyList<string> GetImporters(ContentCookInput input)
        => input.Kind switch
        {
            ContentCookAssetKind.Material => ["material_descriptor"],
            ContentCookAssetKind.Geometry => ["geometry_descriptor"],
            ContentCookAssetKind.Scene => ["scene_descriptor", "script_sidecar"],
            ContentCookAssetKind.Texture => ["texture"],
            ContentCookAssetKind.ForeignSource => [Path.GetExtension(input.SourceCookPath).Equals(".fbx", StringComparison.OrdinalIgnoreCase) ? "fbx" : "gltf"],
            ContentCookAssetKind.Input => ["input"],
            ContentCookAssetKind.Script => ["script"],
            ContentCookAssetKind.BufferContainer => ["buffer_container"],
            _ => throw new ArgumentOutOfRangeException(nameof(input), input.Kind, "Unsupported manifest job asset kind."),
        };

    /// <summary>
    /// Gets the mounts a manifest declares: the project's committed source mounts, with their committed paths or as
    /// placeholders, and its valid cooked mounts, as placeholders. The cook's mounts file binds them, and adds the mounts
    /// only a <c>.mounts.json</c> file declares.
    /// </summary>
    /// <param name="project">The project.</param>
    /// <returns>The mounts a manifest declares.</returns>
    internal static List<ContentImportMount> CreateMounts(ProjectContext project)
        => [.. project.DeclaredMounts
            .Where(static mount => mount.Kind == MountKind.Source)
            .Select(static mount => new ContentImportMount(mount.Name, mount.Path?.Replace('\\', '/').Trim('/')))
            .Concat(project.LibraryMounts.Select(static mount => new ContentImportMount(mount.Name, Path: null) { Kind = nameof(MountKind.Cooked) }))];

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
            "script-sidecar:" + sceneJob.AttributedSource,
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

    // Pairs each generated scene descriptor, in path order, with the cook input that stands for it.
    private static List<(SceneDescriptorGenerationResult Descriptor, ContentCookInput Input)> CreateSceneInputs(
        ContentCookScope scope,
        string mountName,
        IReadOnlyList<SceneDescriptorGenerationResult> sceneDescriptors)
        => [.. sceneDescriptors.OrderBy(static item => item.DescriptorPath, StringComparer.Ordinal)
            .Select(descriptor => (Descriptor: descriptor, Input: new ContentCookInput(
                descriptor.SceneAssetUri,
                ContentCookAssetKind.Scene,
                mountName,
                CookPaths.Of(scope.Project, FileSystemPath.From(descriptor.DescriptorPath)),
                FileSystemPath.From(descriptor.DescriptorPath),
                ContentCookInputRole.GeneratedDescriptor)
            {
                DerivedFrom = DerivedFromOf(scope, descriptor.SceneAssetUri),
            }))];

    // The authored scene a generated descriptor derives from: the scope's own input of that scene.
    private static string? DerivedFromOf(ContentCookScope scope, AssetPath scene)
        => scope.Inputs.FirstOrDefault(input => input.AssetUri == scene && input.Role != ContentCookInputRole.GeneratedDescriptor) is { } authored
            ? authored.SourceCookPath
            : scope.Inputs.FirstOrDefault(input => input.AssetUri == scene)?.DerivedFrom;

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

    private static string GetJobType(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Material => "material-descriptor",
            ContentCookAssetKind.Geometry => "geometry-descriptor",
            ContentCookAssetKind.Scene => "scene-descriptor",
            ContentCookAssetKind.Texture => "texture-descriptor",
            ContentCookAssetKind.Input => "input",
            ContentCookAssetKind.Script => "script",
            ContentCookAssetKind.BufferContainer => "buffer-container",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

    private static string BuildJobId(ContentCookInput input) => BuildJobId(input.Kind, input.SourceCookPath);

    private static string BuildJobId(ContentCookAssetKind kind, string source) => GetJobKindPrefix(kind) + ":" + source;

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
            ContentCookAssetKind.BufferContainer => "buffers",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

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
                     .ThenBy(static item => item.SourceCookPath, StringComparer.Ordinal))
        {
            var dependencies = scope.InputDependencies.TryGetValue(input.AssetUri, out var declared)
                ? declared.Where(jobIds.ContainsKey).Select(uri => jobIds[uri]).Distinct(StringComparer.Ordinal).ToArray() : [];
            var job = (scope.NativeJobs.GetValueOrDefault(input.AssetUri) ?? this.BuildJob(input, dependencies)) with { DependsOn = dependencies };
            jobs.Add(job);
        }

        return jobs;
    }
}
