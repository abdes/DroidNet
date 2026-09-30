// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
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
        var prepared = new List<PreparedSource>();
        var generated = new Dictionary<Uri, ContentCookInput>();
        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        foreach (var input in inputs)
        {
            if (reused.ContainsKey(input.AssetUri))
            {
                continue;
            }

            var source = await this.PrepareAsync(input, cancellationToken).ConfigureAwait(false);
            prepared.Add(source);
            if (source.Scene is { } scene)
            {
                diagnostics.AddRange(scene.Diagnostics);
                foreach (var dependency in scene.Dependencies.Where(static item => item.Role == ContentCookInputRole.GeneratedDescriptor))
                {
                    generated[dependency.AssetUri] = dependency;
                }
            }
        }

        if (diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Error))
        {
            return new([], [], [], diagnostics.ToImmutable());
        }

        var generatedJobs = generated.Values.Select(input => recipes.BuildJob(this.Rebase(input), [])).ToArray();
        var execution = new ContentSourceAnalysisExecution(
            operation.OperationId,
            operation.Project.ProjectRoot,
            this.root,
            [.. prepared.Select(static source => source.Job), .. generatedJobs]) { Artifacts = artifacts };
        var report = await native.AnalyzeSourcesAsync(execution, cancellationToken).ConfigureAwait(false);
        foreach (var job in report.Jobs)
        {
            var owner = prepared.FirstOrDefault(source => string.Equals(source.Job.Id, job.Id, StringComparison.Ordinal))?.Input;
            diagnostics.AddRange(job.Diagnostics.Select(issue => issue with { AffectedVirtualPath = owner?.AssetUri.AbsolutePath }));
            foreach (var path in job.Files.Select(static file => file.Path).Concat(job.Observations.Select(static observation => observation.Path)).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                var relative = Path.GetRelativePath(operation.Project.ProjectRoot, path).Replace('\\', '/');
                if (Path.IsPathRooted(relative) || relative is ".." || relative.StartsWith("../", StringComparison.Ordinal))
                {
                    diagnostics.Add(new()
                    {
                        OperationId = operation.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                        Code = "asset_cook.source_outside_project", Message = $"Cook dependency '{path}' is outside the project. Retain it inside the project before cooking.",
                        AffectedPath = path, AffectedVirtualPath = owner?.AssetUri.AbsolutePath,
                    });
                }
            }
        }

        foreach (var job in report.Jobs.Where(static job => !job.Complete && !job.Diagnostics.Any(static issue => issue.Severity >= DiagnosticSeverity.Error)))
        {
            var owner = prepared.FirstOrDefault(source => string.Equals(source.Job.Id, job.Id, StringComparison.Ordinal))?.Input;
            diagnostics.Add(new()
            {
                OperationId = operation.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                Code = "asset_cook.analysis_incomplete", Message = "Native source analysis did not complete.",
                AffectedPath = owner?.SourceAbsolutePath ?? job.SourcePath, AffectedVirtualPath = owner?.AssetUri.AbsolutePath,
            });
        }

        var generatedPaths = generated.Values.Select(static input => input.SourceAbsolutePath)
            .Concat(prepared.Where(static source => source.Scene is not null).Select(static source => source.Scene!.DescriptorPath))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var generatedInputs = ImmutableArray.CreateBuilder<NativeCapturedInput>();
        foreach (var path in generatedPaths)
        {
            var observation = report.Jobs.SelectMany(static job => job.Observations)
                .FirstOrDefault(item => string.Equals(item.Path, path, StringComparison.OrdinalIgnoreCase));
            if (observation?.Metadata is not { } metadata)
            {
                continue;
            }

            var digest = observation.Reads.FirstOrDefault(static read => read.Offset == 0 && (read.MaxBytes == 0));
            if (digest is null)
            {
                throw new InvalidDataException("Native analysis did not retain the complete generated descriptor.");
            }

            generatedInputs.Add(new(path, true, metadata, new(path, metadata.Size, digest.Sha256)));
        }

        var analyzed = ImmutableArray.CreateBuilder<CookSourceFacts>();
        analyzed.AddRange(reused.Values);
        for (var index = 0; index < prepared.Count; index++)
        {
            var source = prepared[index];
            var facts = report.Jobs[index];
            if (!facts.Complete || diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
                && string.Equals(issue.AffectedVirtualPath, source.Input.AssetUri.AbsolutePath, StringComparison.Ordinal)))
            {
                continue;
            }

            var files = await CookSavedSourceReader.ReadNativeInputsAsync(documents, facts, operation.Project.ProjectRoot, generatedPaths, cancellationToken).ConfigureAwait(false);
            analyzed.Add(new(
                source.Input,
                facts.Outputs,
                facts.References,
                [.. source.ManagedFiles, .. files.Select(file => string.Equals(file.SourcePath, source.Input.SourceAbsolutePath, StringComparison.OrdinalIgnoreCase)
                    ? file with { AssetUri = source.Input.AssetUri } : file)]) { Job = source.Job, Scene = source.Scene });
        }

        return new(analyzed.ToImmutable(), [.. generated.Values.Select(this.Rebase)], generatedInputs.ToImmutable(), diagnostics.ToImmutable())
        {
            BuiltinOwners = builtinOwners,
        };
    }

    private async Task<PreparedSource> PrepareAsync(ContentCookInput input, CancellationToken cancellationToken, CookInputSnapshot? snapshot = null)
    {
        var managed = ImmutableArray.CreateBuilder<CookSnapshotInput>();
        if (input.Kind == ContentCookAssetKind.Scene)
        {
            var sourcePath = snapshot is null ? input.SourceAbsolutePath : Path.Combine(snapshot.InputRoot, input.SourceRelativePath);
            var bytes = await CookSavedSourceReader.ReadAsync(documents, sourcePath, cancellationToken).ConfigureAwait(false);
            managed.Add(new(input.AssetUri, input.SourceAbsolutePath, input.SourceRelativePath, Convert.ToHexString(SHA256.HashData(bytes))));
            var project = new Project(new ProjectInfo(
                operation.Project.ProjectId,
                operation.Project.Name,
                operation.Project.Category,
                operation.Project.ProjectRoot,
                operation.Project.Thumbnail)
            {
                AuthoringMounts = [.. operation.Project.AuthoringMounts], LocalFolderMounts = [.. operation.Project.LocalFolderMounts],
            }) { Name = operation.Project.Name };
            foreach (var known in operation.Project.Scenes)
            {
                project.Scenes.Add(new Scene(project) { Id = known.Id, Name = known.Name });
            }

            var stream = new MemoryStream(bytes, writable: false);
            await using var lifetime = stream.ConfigureAwait(false);
            var scene = await new SceneSerializer(project).DeserializeAsync(stream).ConfigureAwait(false);
            var hasBuiltins = scene.AllNodes.SelectMany(static node => node.Components.OfType<GeometryComponent>())
                .Any(static geometry => geometry.Geometry?.Uri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase) == true
                    || geometry.OverrideSlots.OfType<Oxygen.Editor.World.Slots.MaterialsSlot>().Any(static slot => slot.Material.Uri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase)));
            if (hasBuiltins && this.catalog is null)
            {
                this.catalog = await ((IBuiltinGeometryCatalogProvider)native).GetBuiltinGeometryCatalogAsync(this.root, AssetUris.ContentMountPoint, cancellationToken, artifacts).ConfigureAwait(false);
            }

            var scope = new ContentCookScope(operation.Project, scopes.CreateScope(operation.Project), [input], CookTargetKind.Asset)
            {
                Artifacts = artifacts, PreparationRoot = this.root, BuiltinCatalog = this.catalog, PreparedBuiltins = this.builtins,
            };
            var projection = await scenes.GenerateAsync(scene, scope, cancellationToken).ConfigureAwait(false);
            projection = projection with { Dependencies = [.. projection.Dependencies.Select(this.Rebase)] };
            foreach (var builtin in projection.Dependencies.Where(static item => item.Role == ContentCookInputRole.GeneratedDescriptor))
            {
                this.builtins[builtin.AssetUri] = builtin;
            }

            var projected = this.Rebase(input with { SourceAbsolutePath = projection.DescriptorPath, Role = ContentCookInputRole.GeneratedDescriptor });
            return new(input, recipes.BuildJob(projected, []), managed.ToImmutable(), projection);
        }

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

        return new(input, recipes.BuildJob(input, [], settings), managed.ToImmutable(), null);
    }

    private ContentCookInput Rebase(ContentCookInput input)
        => input with { SourceRelativePath = Path.GetRelativePath(operation.Project.ProjectRoot, input.SourceAbsolutePath).Replace('\\', '/') };

    private sealed record PreparedSource(ContentCookInput Input, ContentImportJob Job, ImmutableArray<CookSnapshotInput> ManagedFiles, SceneDescriptorGenerationResult? Scene);
}
