// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;

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

        var jobs = CreateDependencyJobs(scope.Inputs);

        return new ContentImportManifest(
            Version: 1,
            Output: ContentPipelinePaths.GetCookedMountRoot(scope.Project.ProjectRoot, mountName),
            Layout: new ContentImportLayout(ContentPipelinePaths.GetVirtualMountRoot(mountName)),
            Jobs: jobs);
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
        var jobs = new List<ContentImportJob>();
        var jobIdsBySource = new Dictionary<string, string>(StringComparer.Ordinal);
        var materialJobIds = new List<string>();
        foreach (var dependency in sceneDescriptors
                     .SelectMany(static descriptor => descriptor.Dependencies)
                     .Concat(scope.Inputs.Where(static input => input.Kind is not ContentCookAssetKind.Scene))
                     .OrderBy(static input => input.Kind)
                     .ThenBy(static input => input.SourceRelativePath, StringComparer.Ordinal))
        {
            if (jobIdsBySource.ContainsKey(dependency.SourceRelativePath))
            {
                continue;
            }

            var dependencyJob = CreateJob(
                dependency,
                dependency.Kind == ContentCookAssetKind.Geometry ? materialJobIds : []);
            jobs.Add(dependencyJob);
            jobIdsBySource.Add(dependency.SourceRelativePath, dependencyJob.Id);
            if (dependency.Kind == ContentCookAssetKind.Material)
            {
                materialJobIds.Add(dependencyJob.Id);
            }
        }

        foreach (var descriptor in sceneDescriptors.OrderBy(static item => item.DescriptorPath, StringComparer.Ordinal))
        {
            var dependencyIds = descriptor.Dependencies
                .Select(dependency => jobIdsBySource[dependency.SourceRelativePath])
                .Distinct(StringComparer.Ordinal)
                .ToList();
            var sceneDescriptorInput = new ContentCookInput(
                descriptor.SceneAssetUri,
                ContentCookAssetKind.Scene,
                mountName,
                ToProjectRelativePath(scope.Project.ProjectRoot, descriptor.DescriptorPath),
                descriptor.DescriptorPath,
                descriptor.DescriptorVirtualPath,
                ContentCookInputRole.GeneratedDescriptor);
            jobs.Add(CreateJob(sceneDescriptorInput, dependencyIds));
        }

        return new ContentImportManifest(
            Version: 1,
            Output: ContentPipelinePaths.GetCookedMountRoot(scope.Project.ProjectRoot, mountName),
            Layout: new ContentImportLayout(ContentPipelinePaths.GetVirtualMountRoot(mountName)),
            Jobs: jobs);
    }

    private static List<ContentImportJob> CreateDependencyJobs(IReadOnlyList<ContentCookInput> inputs)
    {
        var jobs = new List<ContentImportJob>();
        var materialJobIds = new List<string>();
        foreach (var input in inputs
                     .OrderBy(static item => item.Kind)
                     .ThenBy(static item => item.SourceRelativePath, StringComparer.Ordinal))
        {
            var job = CreateJob(input, input.Kind == ContentCookAssetKind.Geometry ? materialJobIds : []);
            jobs.Add(job);
            if (input.Kind == ContentCookAssetKind.Material)
            {
                materialJobIds.Add(job.Id);
            }
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
        if (scope.Inputs.Any(input => !string.Equals(input.MountName, mountName, StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidOperationException("One content import manifest cannot span multiple authoring mounts.");
        }

        return mountName;
    }

    private static ContentImportJob CreateJob(ContentCookInput input, IReadOnlyList<string> dependsOn)
        => new(
            Id: BuildJobId(input),
            Type: GetJobType(input.Kind),
            Source: NormalizeSource(input.SourceRelativePath),
            DependsOn: dependsOn,
            Output: null,
            Name: Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(input.SourceRelativePath)));

    private static string GetJobType(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Material => "material-descriptor",
            ContentCookAssetKind.Geometry => "geometry-descriptor",
            ContentCookAssetKind.Scene => "scene-descriptor",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

    private static string BuildJobId(ContentCookInput input)
    {
        var builder = new StringBuilder();
        _ = builder.Append(GetJobKindPrefix(input.Kind));
        _ = builder.Append('-');
        var source = NormalizeSource(input.SourceRelativePath);
        foreach (var ch in source)
        {
            _ = builder.Append(char.IsAsciiLetterOrDigit(ch) ? ch : '-');
        }

        return builder.ToString().Trim('-');
    }

    private static string GetJobKindPrefix(ContentCookAssetKind kind)
        => kind switch
        {
            ContentCookAssetKind.Material => "material",
            ContentCookAssetKind.Geometry => "geometry",
            ContentCookAssetKind.Scene => "scene",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported manifest job asset kind."),
        };

    private static string ToProjectRelativePath(string projectRoot, string absolutePath)
        => NormalizeSource(Path.GetRelativePath(projectRoot, absolutePath));

    private static string NormalizeSource(string source)
        => source.Replace('\\', '/').TrimStart('/');
}
