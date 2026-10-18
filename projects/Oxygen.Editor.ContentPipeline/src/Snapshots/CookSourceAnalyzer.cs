// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
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
    ICookDocumentRegistry documents,
    CookProvenance? previous = null) : ICookSourceFactsProvider
{
    private readonly string root = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N"), "analysis", Guid.NewGuid().ToString("N"));
    private readonly Dictionary<Uri, ContentCookInput> builtins = [];
    private BuiltinGeometryCatalog? catalog;

    /// <inheritdoc />
    public async Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        var reused = await this.ReadReusableFactsAsync(inputs, cancellationToken).ConfigureAwait(false);
        var builtinOwners = this.AcceptedBuiltinOwners();
        if (reused.Count == inputs.Count)
        {
            return new([.. reused.Values], [], [], []) { BuiltinOwners = builtinOwners };
        }

        _ = Directory.CreateDirectory(this.root);
        var (prepared, generated, diagnostics) = await this.PrepareSourcesAsync(
            inputs.Where(input => !reused.ContainsKey(input.AssetUri)),
            cancellationToken).ConfigureAwait(false);
        if (diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Error))
        {
            return new([], [], [], diagnostics.ToImmutable());
        }

        var generatedJobs = generated.Values.Select(input => recipes.BuildJob(this.Rebase(input), [])).ToArray();
        var execution = new ContentSourceAnalysisExecution(
            operation.OperationId,
            operation.Project.ProjectRoot,
            this.root,
            [.. prepared.Select(static source => source.Job), .. generatedJobs])
        { Artifacts = artifacts };
        var report = await native.AnalyzeSourcesAsync(execution, cancellationToken).ConfigureAwait(false);
        this.AddReportDiagnostics(report, prepared, diagnostics);

        var generatedPaths = generated.Values.Select(static input => input.SourceAbsolutePath)
            .Concat(prepared.Where(static source => source.Scene is not null).Select(static source => source.Scene!.DescriptorPath))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var analyzed = await this.CollectFactsAsync(reused, prepared, report, diagnostics, generatedPaths, cancellationToken).ConfigureAwait(false);
        return new(analyzed, [.. generated.Values.Select(this.Rebase)], CaptureGeneratedInputs(report, generatedPaths), diagnostics.ToImmutable())
        {
            BuiltinOwners = builtinOwners,
        };
    }

    // The descriptors native analysis read but the editor generated are captured from the analysis, not from disk.
    private static ImmutableArray<NativeCapturedInput> CaptureGeneratedInputs(NativeSourceAnalysisReport report, HashSet<string> generatedPaths)
    {
        var generatedInputs = ImmutableArray.CreateBuilder<NativeCapturedInput>();
        foreach (var path in generatedPaths)
        {
            var observation = report.Jobs.SelectMany(static job => job.Observations)
                .FirstOrDefault(item => string.Equals(item.Path, path, StringComparison.OrdinalIgnoreCase));
            if (observation?.Metadata is not { } metadata)
            {
                continue;
            }

            var digest = observation.Reads.FirstOrDefault(static read => read.Offset == 0 && (read.MaxBytes == 0))
                ?? throw new InvalidDataException("Native analysis did not retain the complete generated descriptor.");
            generatedInputs.Add(new(path, true, metadata, new(path, metadata.Size, digest.Sha256)));
        }

        return generatedInputs.ToImmutable();
    }

    // The scripts and assets a scene's slots use are cooked by its sidecar job, which native analysis does not see.
    private static IEnumerable<NativeLogicalDependency> ScriptReferences(SceneDescriptorGenerationResult? scene)
        => scene?.ScriptReferences.Select(static uri =>
        {
            var path = Uri.UnescapeDataString(uri.AbsolutePath);
            path = path.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? path[..^".json".Length] : path;
            var kind = path.EndsWith(AssetFileTypes.Script, StringComparison.OrdinalIgnoreCase) ? "script" : "asset";
            return new NativeLogicalDependency(path, kind, ObjectPath: "scripting", Required: true);
        }) ?? [];

    private async Task<(List<PreparedSource> Prepared, Dictionary<Uri, ContentCookInput> Generated, ImmutableArray<DiagnosticRecord>.Builder Diagnostics)> PrepareSourcesAsync(
        IEnumerable<ContentCookInput> inputs,
        CancellationToken cancellationToken)
    {
        var prepared = new List<PreparedSource>();
        var generated = new Dictionary<Uri, ContentCookInput>();
        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        foreach (var input in inputs)
        {
            var source = await this.PrepareAsync(input, cancellationToken).ConfigureAwait(false);
            prepared.Add(source);
            diagnostics.AddRange(source.Diagnostics);
            if (source.Scene is { } scene)
            {
                diagnostics.AddRange(scene.Diagnostics);
                foreach (var dependency in scene.Dependencies.Where(static item => item.Role == ContentCookInputRole.GeneratedDescriptor))
                {
                    generated[dependency.AssetUri] = dependency;
                }
            }
        }

        return (prepared, generated, diagnostics);
    }

    // A job's own diagnostics, a dependency outside the project, and an analysis that did not complete.
    private void AddReportDiagnostics(NativeSourceAnalysisReport report, List<PreparedSource> prepared, ImmutableArray<DiagnosticRecord>.Builder diagnostics)
    {
        foreach (var job in report.Jobs)
        {
            var owner = prepared.FirstOrDefault(source => string.Equals(source.Job.Id, job.Id, StringComparison.Ordinal))?.Input;
            var ownerPath = owner is null ? null : AssetUriHelper.GetVirtualPath(owner.AssetUri);
            diagnostics.AddRange(job.Diagnostics.Select(issue => issue with { AffectedVirtualPath = ownerPath }));
            foreach (var path in job.Files.Select(static file => file.Path).Concat(job.Observations.Select(static observation => observation.Path)).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                var relative = Path.GetRelativePath(operation.Project.ProjectRoot, path).Replace('\\', '/');
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
                    AffectedPath = owner?.SourceAbsolutePath ?? job.SourcePath,
                    AffectedVirtualPath = ownerPath,
                });
            }
        }
    }

    // The facts of each prepared source that analysis completed without an error, after the reused ones.
    private async Task<ImmutableArray<CookSourceFacts>> CollectFactsAsync(
        Dictionary<Uri, CookSourceFacts> reused,
        List<PreparedSource> prepared,
        NativeSourceAnalysisReport report,
        ImmutableArray<DiagnosticRecord>.Builder diagnostics,
        HashSet<string> generatedPaths,
        CancellationToken cancellationToken)
    {
        var analyzed = ImmutableArray.CreateBuilder<CookSourceFacts>();
        analyzed.AddRange(reused.Values);
        for (var index = 0; index < prepared.Count; index++)
        {
            var source = prepared[index];
            var facts = report.Jobs[index];
            if (!facts.Complete || diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
                && string.Equals(issue.AffectedVirtualPath, AssetUriHelper.GetVirtualPath(source.Input.AssetUri), StringComparison.Ordinal)))
            {
                continue;
            }

            var files = await CookSavedSourceReader.ReadNativeInputsAsync(documents, facts, operation.Project.ProjectRoot, generatedPaths, cancellationToken).ConfigureAwait(false);
            analyzed.Add(new(
                source.Input,
                facts.Outputs,
                [.. facts.References, .. ScriptReferences(source.Scene)],
                [.. source.ManagedFiles, .. files.Select(file => string.Equals(file.SourcePath, source.Input.SourceAbsolutePath, StringComparison.OrdinalIgnoreCase)
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
            var bytes = await CookSavedSourceReader.ReadAsync(documents, settingsPath, cancellationToken).ConfigureAwait(false);
            managed.Add(new(null, settingsPath, input.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix, Convert.ToHexString(SHA256.HashData(bytes))));
            if (input.Kind == ContentCookAssetKind.ForeignSource)
            {
                settings = NativeSceneImportSettings.Parse(bytes);
                input = input with { MountName = settings.MountPoint, OutputVirtualPath = null, OutputNamespaces = settings.OutputPrefixes };
            }
        }
        else
        {
            managed.Add(new(null, settingsPath, input.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix, string.Empty, CookSnapshotInputKind.Absent));
        }

        var textureDiagnostics = input.Kind == ContentCookAssetKind.Material
            ? await this.ValidateMaterialTextureSourcesAsync(input, cancellationToken).ConfigureAwait(false)
            : [];
        return new(input, recipes.BuildJob(input, [], settings), managed.ToImmutable(), Scene: null) { Diagnostics = textureDiagnostics };
    }

    // A scene is projected to the descriptor the engine cooks; its builtins are prepared once per analysis.
    private async Task<PreparedSource> PrepareSceneAsync(
        ContentCookInput input,
        CookInputSnapshot? snapshot,
        CancellationToken cancellationToken)
    {
        var sourcePath = snapshot is null ? input.SourceAbsolutePath : Path.Combine(snapshot.InputRoot, input.SourceRelativePath);
        var bytes = await CookSavedSourceReader.ReadAsync(documents, sourcePath, cancellationToken).ConfigureAwait(false);
        ImmutableArray<CookSnapshotInput> managed = [new(input.AssetUri, input.SourceAbsolutePath, input.SourceRelativePath, Convert.ToHexString(SHA256.HashData(bytes)))];
        var stream = new MemoryStream(bytes, writable: false);
        await using var lifetime = stream.ConfigureAwait(false);
        var scene = await SceneSerializer.DeserializeAsync(stream).ConfigureAwait(false);

        // A scene's name is its file name, whatever the file says; a scene a relocation renamed keeps its old name
        // inside the file until it is saved again.
        scene.Name = Path.GetFileName(input.SourceAbsolutePath)[..^AssetFileTypes.SceneDescriptor.Length];
        var hasBuiltins = scene.AllNodes.SelectMany(static node => node.Components.OfType<GeometryComponent>())
            .Any(static geometry => geometry.Geometry.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase)
                || geometry.OverrideSlots.OfType<Oxygen.Editor.World.Slots.MaterialsSlot>().Any(static slot => slot.Material.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase)));
        if (hasBuiltins && this.catalog is null)
        {
            this.catalog = await ((IBuiltinGeometryCatalogProvider)native).GetBuiltinGeometryCatalogAsync(this.root, AssetUris.ContentMountPoint, cancellationToken, artifacts).ConfigureAwait(false);
        }

        var scope = new ContentCookScope(operation.Project, scopes.CreateScope(operation.Project), [input], CookTargetKind.Asset)
        {
            Artifacts = artifacts,
            PreparationRoot = this.root,
            BuiltinCatalog = this.catalog,
            PreparedBuiltins = this.builtins,
        };
        var projection = await scenes.GenerateAsync(scene, scope, cancellationToken).ConfigureAwait(false);
        projection = projection with { Dependencies = [.. projection.Dependencies.Select(this.Rebase)] };
        foreach (var builtin in projection.Dependencies.Where(static item => item.Role == ContentCookInputRole.GeneratedDescriptor))
        {
            this.builtins[builtin.AssetUri] = builtin;
        }

        var projected = this.Rebase(input with { SourceAbsolutePath = projection.DescriptorPath, Role = ContentCookInputRole.GeneratedDescriptor });
        return new(input, recipes.BuildJob(projected, []), managed, projection);
    }

    private async Task<ImmutableArray<DiagnosticRecord>> ValidateMaterialTextureSourcesAsync(ContentCookInput input, CancellationToken cancellationToken)
    {
        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        var materialBytes = await CookSavedSourceReader.ReadAsync(documents, input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
        JsonDocument material;
        try
        {
            material = JsonDocument.Parse(materialBytes);
        }
        catch (JsonException exception)
        {
            diagnostics.Add(this.CreateTextureReferenceDiagnostic(input, "<unknown>", input.SourceAbsolutePath, "material descriptor is invalid JSON", exception.Message));
            return diagnostics.ToImmutable();
        }

        using (material)
        {
            var materialRoot = material.RootElement;
            if (materialRoot.ValueKind != JsonValueKind.Object || !materialRoot.TryGetProperty("textures", out var textures))
            {
                return diagnostics.ToImmutable();
            }

            if (textures.ValueKind != JsonValueKind.Object)
            {
                diagnostics.Add(this.CreateTextureReferenceDiagnostic(input, "<unknown>", input.SourceAbsolutePath, "material texture bindings are invalid", details: null));
                return diagnostics.ToImmutable();
            }

            foreach (var binding in textures.EnumerateObject())
            {
                if (await this.ValidateTextureBindingAsync(input, binding, cancellationToken).ConfigureAwait(false) is { } problem)
                {
                    diagnostics.Add(problem);
                }
            }
        }

        return diagnostics.ToImmutable();
    }

    // A binding names a texture by its virtual path; an authored texture's descriptor must exist and name an image.
    private async Task<DiagnosticRecord?> ValidateTextureBindingAsync(ContentCookInput input, JsonProperty binding, CancellationToken cancellationToken)
    {
        var virtualPath = binding.Value.ValueKind == JsonValueKind.Object
            && binding.Value.TryGetProperty("virtual_path", out var pathValue)
            && pathValue.ValueKind == JsonValueKind.String
            ? pathValue.GetString()
            : null;
        if (string.IsNullOrWhiteSpace(virtualPath))
        {
            return this.CreateTextureReferenceDiagnostic(input, binding.Name, input.SourceAbsolutePath, "binding has no valid virtual_path", details: null);
        }

        if (!VirtualPath.IsCanonical(virtualPath) || !virtualPath.EndsWith(AssetFileTypes.Texture, StringComparison.Ordinal))
        {
            return this.CreateTextureReferenceDiagnostic(input, binding.Name, virtualPath, "path is not a canonical named texture", details: null);
        }

        var textureUri = new Uri(AssetUris.Scheme + ":///" + string.Join('/', virtualPath.TrimStart('/').Split('/').Select(Uri.EscapeDataString)));
        if (!CookInputResolver.IsAuthoringUri(operation.Project, textureUri))
        {
            return null;
        }

        var textureDescriptorPath = this.ResolveTextureDescriptorPath(virtualPath);
        return textureDescriptorPath is null || !File.Exists(textureDescriptorPath)
            ? this.CreateTextureReferenceDiagnostic(input, binding.Name, textureDescriptorPath ?? virtualPath, "texture descriptor is missing", details: null)
            : await this.ValidateTextureImageAsync(input, binding.Name, textureDescriptorPath, cancellationToken).ConfigureAwait(false);
    }

    private async Task<DiagnosticRecord?> ValidateTextureImageAsync(ContentCookInput input, string channel, string textureDescriptorPath, CancellationToken cancellationToken)
    {
        try
        {
            using var textureDescriptor = JsonDocument.Parse(await File.ReadAllBytesAsync(textureDescriptorPath, cancellationToken).ConfigureAwait(false));
            var descriptorRoot = textureDescriptor.RootElement;
            var source = descriptorRoot.ValueKind == JsonValueKind.Object
                && descriptorRoot.TryGetProperty("source", out var sourceValue)
                && sourceValue.ValueKind == JsonValueKind.String
                ? sourceValue.GetString()
                : null;
            if (string.IsNullOrWhiteSpace(source))
            {
                return this.CreateTextureReferenceDiagnostic(input, channel, textureDescriptorPath, "texture descriptor has no source image", details: null);
            }

            var imagePath = Path.IsPathRooted(source)
                ? Path.GetFullPath(source)
                : Path.GetFullPath(Path.Combine(Path.GetDirectoryName(textureDescriptorPath)!, source));
            return File.Exists(imagePath) ? null : this.CreateTextureReferenceDiagnostic(input, channel, imagePath, "source image is missing", details: null);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or ArgumentException)
        {
            return this.CreateTextureReferenceDiagnostic(input, channel, textureDescriptorPath, "texture descriptor is invalid", exception.Message);
        }
    }

    private string? ResolveTextureDescriptorPath(string virtualPath)
    {
        var parts = virtualPath.TrimStart('/').Split('/');
        var mount = operation.Project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, parts[0], StringComparison.Ordinal));
        if (mount is null || parts.Length < 2 || parts.Skip(1).Any(static part => part is "" or "." or ".."))
        {
            return null;
        }

        var relative = Path.Combine(parts.Skip(1).ToArray()) + ".json";
        var mountRoot = Path.GetFullPath(Path.Combine(operation.Project.ProjectRoot, mount.RelativePath));
        var descriptor = Path.GetFullPath(Path.Combine(mountRoot, relative));
        var rootedMount = mountRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return descriptor.StartsWith(rootedMount, StringComparison.OrdinalIgnoreCase) ? descriptor : null;
    }

    private DiagnosticRecord CreateTextureReferenceDiagnostic(ContentCookInput input, string channel, string affectedPath, string issue, string? details)
        => new()
        {
            OperationId = operation.OperationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = "material.texture_reference_invalid",
            Message = $"Material '{AssetUriHelper.GetVirtualPath(input.AssetUri)}' texture channel '{channel}' {issue}.",
            TechnicalMessage = details,
            AffectedPath = affectedPath,
            AffectedVirtualPath = AssetUriHelper.GetVirtualPath(input.AssetUri),
        };

    private ContentCookInput Rebase(ContentCookInput input)
        => input with { SourceRelativePath = Path.GetRelativePath(operation.Project.ProjectRoot, input.SourceAbsolutePath).Replace('\\', '/') };

    private sealed record PreparedSource(ContentCookInput Input, ContentImportJob Job, ImmutableArray<CookSnapshotInput> ManagedFiles, SceneDescriptorGenerationResult? Scene)
    {
        public ImmutableArray<DiagnosticRecord> Diagnostics { get; init; } = [];
    }
}
