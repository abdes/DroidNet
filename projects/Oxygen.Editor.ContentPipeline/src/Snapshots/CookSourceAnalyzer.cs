// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Projects editor-owned source and submits complete native recipes in dependency frontiers.</summary>
internal sealed partial class CookSourceAnalyzer(
    ContentCookOperation operation,
    NativeArtifactLease artifacts,
    IEngineContentPipelineApi native,
    IContentImportManifestBuilder recipes,
    ISceneDescriptorGenerator scenes,
    IProjectCookScopeProvider scopes,
    CookProvenance? previous = null) : ICookSourceFactsProvider
{
    private readonly string root = Path.Combine(operation.Project.ProjectRoot.FullPath, ".build", "cook", operation.OperationId.ToString("N"), "analysis", Guid.NewGuid().ToString("N"));

    /// <inheritdoc />
    public async Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        var reused = await this.ReadReusableFactsAsync(inputs, cancellationToken).ConfigureAwait(false);
        if (reused.Count == inputs.Count)
        {
            return new([.. reused.Values], [], []);
        }

        _ = Directory.CreateDirectory(this.root);
        var (prepared, diagnostics) = await this.PrepareSourcesAsync(
            inputs.Where(input => !reused.ContainsKey(input.AssetUri)),
            cancellationToken).ConfigureAwait(false);
        if (diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Error))
        {
            return new([], [], diagnostics.ToImmutable());
        }

        var execution = new ContentSourceAnalysisExecution(
            operation.OperationId,
            operation.Project.ProjectRoot.FullPath,
            this.root,
            [.. prepared.Select(static source => source.Job)])
        { Artifacts = artifacts };
        var report = await native.AnalyzeSourcesAsync(execution, cancellationToken).ConfigureAwait(false);
        this.AddReportDiagnostics(report, prepared, diagnostics);

        var generatedPaths = prepared.Where(static source => source.Scene is not null)
            .Select(static source => FileSystemPath.From(source.Scene!.DescriptorPath)).ToHashSet();
        var analyzed = await this.CollectFactsAsync(reused, prepared, report, diagnostics, generatedPaths, cancellationToken).ConfigureAwait(false);
        return new(analyzed, CaptureGeneratedInputs(report, generatedPaths), diagnostics.ToImmutable());
    }

    // The descriptors native analysis read but the editor generated are captured from the analysis, not from disk.
    private static ImmutableArray<NativeCapturedInput> CaptureGeneratedInputs(NativeSourceAnalysisReport report, HashSet<FileSystemPath> generatedPaths)
    {
        var generatedInputs = ImmutableArray.CreateBuilder<NativeCapturedInput>();
        foreach (var path in generatedPaths)
        {
            var observation = report.Jobs.SelectMany(static job => job.Observations)
                .FirstOrDefault(item => FileSystemPath.From(item.Path) == path);
            if (observation?.Metadata is not { } metadata)
            {
                continue;
            }

            var digest = observation.Reads.FirstOrDefault(static read => read.Offset == 0 && (read.MaxBytes == 0))
                ?? throw new InvalidDataException("Native analysis did not retain the complete generated descriptor.");
            generatedInputs.Add(new(path.FullPath, Exists: true, metadata, new(path.FullPath, metadata.Size, digest.Sha256)));
        }

        return generatedInputs.ToImmutable();
    }

    // The scripts and assets a scene's slots use are cooked by its sidecar job, which native analysis does not see.
    private static IEnumerable<NativeLogicalDependency> ScriptReferences(SceneDescriptorGenerationResult? scene)
        => scene?.ScriptReferences.Select(static uri =>
        {
            var path = uri.VirtualPath;
            var kind = ContentCookAssetKinds.Of(path) == ContentCookAssetKind.Script ? "script" : "asset";
            return new NativeLogicalDependency(path, kind, ObjectPath: "scripting", Required: true);
        }) ?? [];

    private async Task<(List<PreparedSource> Prepared, ImmutableArray<DiagnosticRecord>.Builder Diagnostics)> PrepareSourcesAsync(
        IEnumerable<ContentCookInput> inputs,
        CancellationToken cancellationToken)
    {
        var prepared = new List<PreparedSource>();
        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        foreach (var input in inputs)
        {
            var source = await this.PrepareAsync(input, cancellationToken).ConfigureAwait(false);
            prepared.Add(source);
            diagnostics.AddRange(source.Diagnostics);
            if (source.Scene is { } scene)
            {
                diagnostics.AddRange(scene.Diagnostics);
            }
        }

        return (prepared, diagnostics);
    }

    // A job's own diagnostics, a dependency outside the project, and an analysis that did not complete.
    private void AddReportDiagnostics(NativeSourceAnalysisReport report, List<PreparedSource> prepared, ImmutableArray<DiagnosticRecord>.Builder diagnostics)
    {
        foreach (var job in report.Jobs)
        {
            var owner = prepared.FirstOrDefault(source => string.Equals(source.Job.Id, job.Id, StringComparison.Ordinal))?.Input;
            var ownerPath = owner is null ? null : owner.AssetUri.VirtualPath;
            diagnostics.AddRange(job.Diagnostics.Select(issue => issue with { AffectedVirtualPath = ownerPath }));
            foreach (var path in job.Files.Select(static file => file.Path).Concat(job.Observations.Select(static observation => observation.Path)).DistinctBy(FileSystemPath.From))
            {
                var relative = Path.GetRelativePath(operation.Project.ProjectRoot.FullPath, path).Replace('\\', '/');
                if (Path.IsPathRooted(relative) || relative is ".." || relative.StartsWith("../", StringComparison.Ordinal))
                {
                    diagnostics.Add(new()
                    {
                        OperationId = operation.OperationId,
                        Domain = FailureDomain.ContentPipeline,
                        Severity = DiagnosticSeverity.Error,
                        Code = "asset_cook.source_outside_project",
                        Message = $"Cook dependency '{path}' is outside the project. Retain it inside the project before cooking.",
                        AffectedPath = path,
                        AffectedVirtualPath = ownerPath,
                    });
                }
            }

            if (!job.Complete && !job.Diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Error))
            {
                diagnostics.Add(new()
                {
                    OperationId = operation.OperationId,
                    Domain = FailureDomain.ContentPipeline,
                    Severity = DiagnosticSeverity.Error,
                    Code = "asset_cook.analysis_incomplete",
                    Message = "Native source analysis did not complete.",
                    AffectedPath = owner?.SourceAbsolutePath.FullPath ?? job.SourcePath,
                    AffectedVirtualPath = ownerPath,
                });
            }
        }
    }

    // The facts of each prepared source that analysis completed without an error, after the reused ones.
    private async Task<ImmutableArray<CookSourceFacts>> CollectFactsAsync(
        Dictionary<AssetPath, CookSourceFacts> reused,
        List<PreparedSource> prepared,
        NativeSourceAnalysisReport report,
        ImmutableArray<DiagnosticRecord>.Builder diagnostics,
        HashSet<FileSystemPath> generatedPaths,
        CancellationToken cancellationToken)
    {
        var analyzed = ImmutableArray.CreateBuilder<CookSourceFacts>();
        analyzed.AddRange(reused.Values);
        for (var index = 0; index < prepared.Count; index++)
        {
            var source = prepared[index];
            var facts = report.Jobs[index];
            if (!facts.Complete || diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
                && string.Equals(issue.AffectedVirtualPath, source.Input.AssetUri.VirtualPath, StringComparison.Ordinal)))
            {
                continue;
            }

            var files = await CookSavedSourceReader.ReadNativeInputsAsync(facts, operation.Project.ProjectRoot, generatedPaths, cancellationToken).ConfigureAwait(false);
            analyzed.Add(new(
                source.Input,
                facts.Outputs,
                [.. facts.References, .. ScriptReferences(source.Scene)],
                [.. source.ManagedFiles, .. files.Select(file => file.SourcePath == source.Input.SourceAbsolutePath
                    ? file with { AssetUri = source.Input.AssetUri } : file)])
            { Job = source.Job, Scene = source.Scene });
        }

        return analyzed.ToImmutable();
    }

    private async Task<PreparedSource> PrepareAsync(ContentCookInput input, CancellationToken cancellationToken, CookInputSnapshot? snapshot = null)
    {
        if (input.Kind == ContentCookAssetKind.Scene)
        {
            return await this.PrepareSceneAsync(input, snapshot, cancellationToken).ConfigureAwait(false);
        }

        var managed = ImmutableArray.CreateBuilder<CookSnapshotInput>();

        NativeSceneImportSettings? settings = null;
        var settingsPath = input.SourceAbsolutePath + NativeSceneImportSettings.SidecarSuffix;
        if (CookSavedSourceReader.Exists(settingsPath))
        {
            var bytes = await CookSavedSourceReader.ReadAsync(settingsPath, cancellationToken).ConfigureAwait(false);
            managed.Add(new(AssetUri: null, FileSystemPath.From(settingsPath), input.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix, Convert.ToHexString(SHA256.HashData(bytes))));
            if (input.Kind == ContentCookAssetKind.ForeignSource)
            {
                settings = NativeSceneImportSettings.Parse(bytes);
            }
        }
        else
        {
            managed.Add(new(AssetUri: null, FileSystemPath.From(settingsPath), input.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix, string.Empty, CookSnapshotInputKind.Absent));
        }

        var textureDiagnostics = input.Kind == ContentCookAssetKind.Material
            ? await this.ValidateMaterialTextureSourcesAsync(input, cancellationToken).ConfigureAwait(false)
            : [];
        return new(input, recipes.BuildJob(input, [], settings), managed.ToImmutable(), Scene: null) { Diagnostics = textureDiagnostics };
    }

    // A scene is projected to the descriptor the engine cooks.
    private async Task<PreparedSource> PrepareSceneAsync(
        ContentCookInput input,
        CookInputSnapshot? snapshot,
        CancellationToken cancellationToken)
    {
        var sourcePath = snapshot is null ? input.SourceAbsolutePath.FullPath : Path.Combine(snapshot.InputRoot, input.SourceRelativePath);
        var bytes = await CookSavedSourceReader.ReadAsync(sourcePath, cancellationToken).ConfigureAwait(false);
        ImmutableArray<CookSnapshotInput> managed = [new(input.AssetUri, input.SourceAbsolutePath, input.SourceRelativePath, Convert.ToHexString(SHA256.HashData(bytes)))];
        var stream = new MemoryStream(bytes, writable: false);
        await using var lifetime = stream.ConfigureAwait(false);
        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        // A scene's name is its file name, whatever the file says; a scene a relocation renamed keeps its old name
        // inside the file until it is saved again.
        scene.Name = input.SourceAbsolutePath.Name[..^AssetFileTypes.SceneDescriptor.Length];
        var scope = new ContentCookScope(operation.Project, scopes.CreateScope(operation.Project), [input], CookTargetKind.Asset)
        {
            Artifacts = artifacts,
            PreparationRoot = this.root,
        };
        var projection = await scenes.GenerateAsync(scene, scope, cancellationToken).ConfigureAwait(false);
        projection = projection with { Dependencies = [.. projection.Dependencies.Select(this.Rebase)] };

        var projected = this.Rebase(input with { SourceAbsolutePath = FileSystemPath.From(projection.DescriptorPath), Role = ContentCookInputRole.GeneratedDescriptor });
        return new(input, recipes.BuildJob(projected, []), managed, projection);
    }

    // A material that cannot be read reports its own problem; one that can has each texture checked.
    private async Task<ImmutableArray<DiagnosticRecord>> ValidateMaterialTextureSourcesAsync(ContentCookInput input, CancellationToken cancellationToken)
    {
        var materialBytes = await CookSavedSourceReader.ReadAsync(input.SourceAbsolutePath.FullPath, cancellationToken).ConfigureAwait(false);
        MaterialSource material;
        try
        {
            material = MaterialSourceReader.Read(materialBytes);
        }
        catch (AssetFormatException exception)
        {
            return [this.CreateSourceDiagnostic(input, exception.Problem)];
        }

        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        foreach (var (channel, texture) in material.TextureReferences)
        {
            if (await this.ValidateTextureBindingAsync(input, channel, texture, cancellationToken).ConfigureAwait(false) is { } problem)
            {
                diagnostics.Add(problem);
            }
        }

        return diagnostics.ToImmutable();
    }

    // A binding names a texture by its virtual path; an authored texture's descriptor must exist and its images too.
    private async Task<DiagnosticRecord?> ValidateTextureBindingAsync(ContentCookInput input, string channel, AssetPath texture, CancellationToken cancellationToken)
    {
        if (!texture.Name.EndsWith(AssetFileTypes.Texture, StringComparison.Ordinal))
        {
            return this.CreateTextureReferenceDiagnostic(input, channel, texture.VirtualPath, "path is not a named texture", details: null);
        }

        if (!CookInputResolver.IsAuthoringUri(operation.Project, texture))
        {
            return null;
        }

        var textureDescriptorPath = this.ResolveTextureDescriptorPath(texture);
        return textureDescriptorPath is null || !operation.Project.FileSystem.File.Exists(textureDescriptorPath)
            ? this.CreateTextureReferenceDiagnostic(input, channel, textureDescriptorPath ?? texture.VirtualPath, "texture descriptor is missing", details: null)
            : await this.ValidateTextureImagesAsync(input, channel, textureDescriptorPath, cancellationToken).ConfigureAwait(false);
    }

    private async Task<DiagnosticRecord?> ValidateTextureImagesAsync(ContentCookInput input, string channel, string textureDescriptorPath, CancellationToken cancellationToken)
    {
        TextureSource texture;
        try
        {
            texture = TextureSourceReader.Read(await operation.Project.FileSystem.File.ReadAllBytesAsync(textureDescriptorPath, cancellationToken).ConfigureAwait(false));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or AssetFormatException)
        {
            return this.CreateTextureReferenceDiagnostic(input, channel, textureDescriptorPath, "texture descriptor is invalid", exception.Message);
        }

        var files = operation.Project.FileSystem;
        return texture.ResolveImages(FileSystemPath.From(textureDescriptorPath), image => files.File.Exists(image.FullPath)).FirstOrDefault(image => !files.File.Exists(image.FullPath)) is { } missing
            ? this.CreateTextureReferenceDiagnostic(input, channel, missing.FullPath, "source image is missing", details: null)
            : null;
    }

    // A texture referenced by identity cooks from its authored descriptor in an authoring mount.
    private string? ResolveTextureDescriptorPath(AssetPath texture)
        => operation.Project.AuthoringMounts.Any(mount => string.Equals(mount.Name, texture.Mount, StringComparison.Ordinal))
            && operation.Project.Find(texture.WithAuthoringSuffix()) is { IsMountRoot: false } descriptor
                ? descriptor.File.FullPath
                : null;

    private DiagnosticRecord CreateSourceDiagnostic(ContentCookInput input, AssetProblem problem)
        => new()
        {
            OperationId = operation.OperationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = problem.Code,
            Message = $"Material '{input.AssetUri.VirtualPath}' cannot be read: {problem.Message}",
            TechnicalMessage = problem.Explanation,
            AffectedPath = input.SourceAbsolutePath.FullPath,
            AffectedVirtualPath = input.AssetUri.VirtualPath,
        };

    private DiagnosticRecord CreateTextureReferenceDiagnostic(ContentCookInput input, string channel, string affectedPath, string issue, string? details)
        => new()
        {
            OperationId = operation.OperationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = "material.texture_reference_invalid",
            Message = $"Material '{input.AssetUri.VirtualPath}' texture channel '{channel}' {issue}.",
            TechnicalMessage = details,
            AffectedPath = affectedPath,
            AffectedVirtualPath = input.AssetUri.VirtualPath,
        };

    private ContentCookInput Rebase(ContentCookInput input)
        => input with { SourceRelativePath = Path.GetRelativePath(operation.Project.ProjectRoot.FullPath, input.SourceAbsolutePath.FullPath).Replace('\\', '/') };

    private sealed record PreparedSource(ContentCookInput Input, ContentImportJob Job, ImmutableArray<CookSnapshotInput> ManagedFiles, SceneDescriptorGenerationResult? Scene)
    {
        public ImmutableArray<DiagnosticRecord> Diagnostics { get; init; } = [];
    }
}
