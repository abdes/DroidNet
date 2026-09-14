// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Captures scoped read-only reports independently of cooking and publication.</summary>
public sealed partial class ContentPipelineService
{
    /// <inheritdoc />
    public async Task<CookedOutputReport> InspectCookedOutputAsync(Uri? scopeUri, CancellationToken cancellationToken, bool validate = false, ProjectContext? expectedProject = null)
    {
        var project = expectedProject ?? this.RequireActiveProject();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        using var projectChanges = this.projectContextService.ProjectChanged.Subscribe(current =>
        {
            if (!ReferenceEquals(current, project))
            {
                cancellation.Cancel();
            }
        });
        var token = cancellation.Token;
        token.ThrowIfCancellationRequested();
        using var publicationRead = await CookOutputLease.AcquireInspectionAsync(project.ProjectRoot, token).ConfigureAwait(false);
        var (provenance, _) = await this.provenanceStore.ReadAsync(project, token).ConfigureAwait(false);
        if (!await this.publication.HasCommittedMetadataUnderLeaseAsync(project, token).ConfigureAwait(false))
        {
            provenance = new(1, project.ProjectId, [], []);
        }

        var requestedRoots = ResolveInspectionRoots(project, scopeUri);
        if (scopeUri is not null && requestedRoots.All(static root => !root.IsLocal))
        {
            var imports = await Import.ImportedSourceIndex.ReadAsync(project, cookDocuments, provenance, token).ConfigureAwait(false);
            requestedRoots = ResolveImportedInspectionRoots(project, scopeUri, requestedRoots, imports.GetInspectionOutputScopes(scopeUri));
        }

        var roots = new List<CookedRootReport>();
        foreach (var root in requestedRoots)
        {
            token.ThrowIfCancellationRequested();
            roots.Add(await this.InspectRootAsync(root, provenance, validate, token).ConfigureAwait(false));
        }

        token.ThrowIfCancellationRequested();
        return new(project.ProjectId, scopeUri, DateTimeOffset.UtcNow, roots);
    }

    private static InspectionRoot[] ResolveInspectionRoots(ProjectContext project, Uri? scope)
    {
        if (scope is not null && (!scope.IsAbsoluteUri || !string.Equals(scope.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            || scope.Query.Length != 0 || scope.Fragment.Length != 0))
        {
            throw new ArgumentException("Inspection scope must be an asset URI without a query or fragment.", nameof(scope));
        }

        var path = scope is null ? string.Empty : AssetUriHelper.GetVirtualPath(scope).Trim('/');
        if (string.Equals(path, "Cooked", StringComparison.OrdinalIgnoreCase))
        {
            path = string.Empty;
        }
        else if (path.StartsWith("Cooked/", StringComparison.OrdinalIgnoreCase))
        {
            path = path["Cooked/".Length..];
        }

        var split = path.IndexOf('/', StringComparison.Ordinal);
        var mountName = split < 0 ? path : path[..split];
        if (project.LocalFolderMounts.FirstOrDefault(mount => string.Equals(mount.Name, mountName, StringComparison.OrdinalIgnoreCase)) is { } local)
        {
            return [new(local.Name, local.AbsolutePath, split < 0 ? string.Empty : path[(split + 1)..], IsLocal: true)];
        }

        var mounts = project.AuthoringMounts.Where(mount => !IsDerivedRootMount(mount)
            && (path.Length == 0 || string.Equals(mountName, "Engine", StringComparison.OrdinalIgnoreCase)
                || string.Equals(mount.Name, mountName, StringComparison.OrdinalIgnoreCase))).ToArray();
        return mounts.Length == 0 ? throw new InvalidDataException("The inspection scope does not belong to a project content mount.")
            : mounts.Select(mount => new InspectionRoot(mount.Name, Path.GetDirectoryName(CookIncrementalPlanner.ResolveOutputPath(project.ProjectRoot, mount.Name, "container.index.bin"))!, path, IsLocal: false)).ToArray();
    }

    private static CookedAssetProvenance[] GetInspectionProvenance(
        InspectionRoot root,
        CookProvenance provenance,
        IReadOnlyDictionary<string, CookProvenance.FileProof> actual)
    {
        var known = root.IsLocal ? null : provenance.Roots.FirstOrDefault(item => string.Equals(item.Mount, root.Name, StringComparison.Ordinal));
        if (known?.SharedFiles.All(Matches) != true)
        {
            return [];
        }

        var valid = known.Assets.Where(asset => Matches(asset.File)).Select(static asset => asset.Entry.VirtualPath).ToHashSet(StringComparer.Ordinal);
        return provenance.Products.SelectMany(product => product.Outputs
                .Where(output => string.Equals(output.RootMount, root.Name, StringComparison.Ordinal) && valid.Contains(output.Asset.VirtualPath))
                .Select(output => new CookedAssetProvenance(output.Asset.CookedAssetUri, product.SourceUri, product.Dependencies)))
            .GroupBy(static association => association.CookedAssetUri)
            .Where(static group => group.Take(2).Count() == 1)
            .Select(static group => group.Single()).ToArray();

        bool Matches(CookProvenance.FileProof proof) => actual.TryGetValue(proof.RelativePath, out var file)
            && file.Size == proof.Size && string.Equals(file.Sha256, proof.Sha256, StringComparison.Ordinal);
    }

    private static bool IsInspectedAssetInScope(CookedAssetEntry asset, InspectionRoot root, IReadOnlyList<CookedAssetProvenance> provenance)
    {
        if (root.Scope.Length == 0)
        {
            return true;
        }

        var nativeScope = root.Scope;
        if (nativeScope.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
            || nativeScope.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
            || nativeScope.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase))
        {
            nativeScope = nativeScope[..^".json".Length];
        }

        return Matches(asset.VirtualPath, nativeScope)
            || root.ImportedScopes.Any(scope => Matches(asset.VirtualPath, scope))
            || (root.IsLocal && asset.DescriptorRelativePath is { } relative && Matches(relative, nativeScope))
            || provenance.Any(item => string.Equals(AssetUriHelper.GetVirtualPath(item.CookedAssetUri), asset.VirtualPath, StringComparison.Ordinal)
                && Matches(AssetUriHelper.GetVirtualPath(item.SourceAssetUri), root.Scope));

        static bool Matches(string candidate, string scope)
        {
            var path = candidate.Trim('/');
            return string.Equals(path, scope, StringComparison.OrdinalIgnoreCase) || path.StartsWith(scope + "/", StringComparison.OrdinalIgnoreCase);
        }
    }

    private static InspectionRoot[] ResolveImportedInspectionRoots(ProjectContext project, Uri scope, InspectionRoot[] ordinary, IEnumerable<string> outputScopes)
    {
        var isModel = Path.GetExtension(scope.AbsolutePath).ToUpperInvariant() is ".GLTF" or ".GLB" or ".FBX";
        var roots = (isModel ? [] : ordinary).ToDictionary(static root => root.Name, StringComparer.OrdinalIgnoreCase);
        foreach (var group in outputScopes.GroupBy(static path => path.Trim('/').Split('/')[0], StringComparer.OrdinalIgnoreCase))
        {
            if (!project.AuthoringMounts.Any(mount => !IsDerivedRootMount(mount) && string.Equals(mount.Name, group.Key, StringComparison.OrdinalIgnoreCase)))
            {
                continue;
            }

            var root = roots.GetValueOrDefault(group.Key) ?? new(group.Key, Path.GetDirectoryName(CookIncrementalPlanner.ResolveOutputPath(project.ProjectRoot, group.Key, "container.index.bin"))!, AssetUriHelper.GetVirtualPath(scope).Trim('/'), IsLocal: false);
            roots[group.Key] = root with { ImportedScopes = [.. group.Select(static path => path.Trim('/'))] };
        }

        return roots.Values.ToArray();
    }

    private async Task<CookedRootReport> InspectRootAsync(InspectionRoot root, CookProvenance provenance, bool validate, CancellationToken cancellationToken)
    {
        if (!Directory.Exists(root.Path))
        {
            return new(root.Name, IsPresent: false, new(root.Path, Succeeded: true, SourceIdentity: null, [], [], []), Validation: null, []);
        }

        try
        {
            var lease = await CookOutputReadLease.AcquireAsync(root.Path, cancellationToken, requireIndex: false).ConfigureAwait(false);
            await using var lifetime = lease.ConfigureAwait(false);
            if (lease.GetFiles().Count == 0)
            {
                return new(root.Name, IsPresent: false, new(root.Path, Succeeded: true, SourceIdentity: null, [], [], []), Validation: null, []);
            }

            var inspection = await this.engineContentPipelineApi.InspectLooseCookedRootAsync(root.Path, cancellationToken).ConfigureAwait(false);
            var validation = validate ? await this.engineContentPipelineApi.ValidateLooseCookedRootAsync(root.Path, cancellationToken).ConfigureAwait(false) : null;
            var known = root.IsLocal ? null : provenance.Roots.FirstOrDefault(item => string.Equals(item.Mount, root.Name, StringComparison.Ordinal));
            var actual = known is not null ? await lease.ReadHashesAsync(cancellationToken).ConfigureAwait(false) : new Dictionary<string, CookProvenance.FileProof>(StringComparer.Ordinal);
            var origins = GetInspectionProvenance(root, provenance, actual);
            if (validation is not null && known is not null)
            {
                var issues = known.SharedFiles.Concat(known.Assets.Select(static asset => asset.File))
                    .Where(proof => !actual.TryGetValue(proof.RelativePath, out var file) || file.Size != proof.Size || !string.Equals(file.Sha256, proof.Sha256, StringComparison.Ordinal))
                    .Select(proof => new DiagnosticRecord
                    {
                        OperationId = Guid.NewGuid(), Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                        Code = ContentPipelineDiagnosticCodes.ValidateFailed,
                        Message = $"Cooked file '{proof.RelativePath}' no longer matches its published content.",
                        AffectedPath = Path.Combine(root.Path, proof.RelativePath),
                    }).ToArray();
                validation = validation with { Succeeded = validation.Succeeded && issues.Length == 0, Diagnostics = [.. validation.Diagnostics, .. issues] };
            }

            var scoped = inspection with
            {
                Assets = inspection.Assets.Where(asset => IsInspectedAssetInScope(asset, root, origins)).ToArray(),
                Files = lease.GetFiles(),
            };
            return new(root.Name, IsPresent: true, scoped, validation, origins);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            var diagnostic = new DiagnosticRecord
            {
                OperationId = Guid.NewGuid(), Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                Code = ContentPipelineDiagnosticCodes.InspectFailed, Message = exception.Message, AffectedPath = root.Path,
                TechnicalMessage = exception.ToString(), ExceptionType = exception.GetType().FullName,
            };
            return new(root.Name, IsPresent: true, new(root.Path, Succeeded: false, SourceIdentity: null, [], [], [diagnostic]), Validation: null, []);
        }
    }

    private sealed record InspectionRoot(string Name, string Path, string Scope, bool IsLocal)
    {
        public IReadOnlyList<string> ImportedScopes { get; init; } = [];
    }
}
