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
        var publicationRead = await this.publication.AcquireReadAsync(project, token).ConfigureAwait(false);
        try
        {
            var provenance = publicationRead.ProductState;

            var requestedRoots = ResolveInspectionRoots(project, publicationRead, scopeUri);
            if (scopeUri is not null && requestedRoots.All(static root => !root.IsLocal))
            {
                var imports = await Import.ImportedSourceIndex.ReadAsync(project, cookDocuments, provenance, token).ConfigureAwait(false);
                requestedRoots = ResolveImportedInspectionRoots(project, publicationRead, scopeUri, requestedRoots, imports.GetInspectionOutputScopes(scopeUri));
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
        catch (ContentPipelineTerminationException failure)
        {
            var retained = publicationRead;
            publicationRead = null;
            var drain = ReleaseInspectionGateAsync(failure.DrainCompletion, retained);
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, drain);
        }
        finally
        {
            publicationRead?.Dispose();
        }
    }

    private static async Task ReleaseInspectionGateAsync(Task drain, IDisposable reader)
    {
        try
        {
            await drain.ConfigureAwait(false);
        }
        finally
        {
            reader.Dispose();
        }
    }

    private static InspectionRoot[] ResolveInspectionRoots(ProjectContext project, CookPublicationReadLease publication, Uri? scope)
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
        if (publication.Roots.FirstOrDefault(root => root.Owner == CookPublicationRootOwner.Library
            && string.Equals(root.Name, mountName, StringComparison.OrdinalIgnoreCase)) is { } local)
        {
            return [new(local.Name, local.LibraryPath, split < 0 ? string.Empty : path[(split + 1)..], IsLocal: true)];
        }

        var mounts = project.AuthoringMounts.Where(mount => !IsDerivedRootMount(mount)
            && (path.Length == 0 || string.Equals(mountName, "Engine", StringComparison.OrdinalIgnoreCase)
                || string.Equals(mount.Name, mountName, StringComparison.OrdinalIgnoreCase))).ToArray();
        return mounts.Length == 0 ? throw new InvalidDataException("The inspection scope does not belong to a project content mount.")
            : mounts.Select(mount => new InspectionRoot(mount.Name, publication.FindProjectRoot(mount.Name), path, IsLocal: false)).ToArray();
    }

    private static CookedAssetProvenance[] GetInspectionProvenance(
        InspectionRoot root,
        CookProvenance provenance,
        CookedInventoryReport actual)
    {
        var known = root.IsLocal ? null : provenance.Roots.FirstOrDefault(item => string.Equals(item.Mount, root.Name, StringComparison.Ordinal));
        if (known is null) { return []; }
        var (shared, valid) = known.Compare(actual, provenance.Products.SelectMany(static product => product.Outputs));
        if (!shared) { return []; }
        return provenance.Products.SelectMany(product => product.Outputs
                .Where(output => string.Equals(output.RootMount, root.Name, StringComparison.Ordinal)
                    && output.Asset.DescriptorRelativePath is { } path && valid.Contains(path))
                .Select(output => new CookedAssetProvenance(output.Asset.CookedAssetUri, product.SourceUri, product.Dependencies)))
            .GroupBy(static association => association.CookedAssetUri)
            .Where(static group => group.Take(2).Count() == 1)
            .Select(static group => group.Single()).ToArray();
    }

    private static bool IsInspectedAssetInScope(CookedAssetEntry asset, InspectionRoot root, IReadOnlyList<CookedAssetProvenance> provenance)
    {
        if (root.Scope.Length == 0)
        {
            return true;
        }

        var nativeScope = root.Scope;
        if (nativeScope.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
            || nativeScope.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
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

    private static InspectionRoot[] ResolveImportedInspectionRoots(ProjectContext project, CookPublicationReadLease publication, Uri scope, InspectionRoot[] ordinary, IEnumerable<string> outputScopes)
    {
        var isModel = Path.GetExtension(scope.AbsolutePath).ToUpperInvariant() is ".GLTF" or ".GLB" or ".FBX";
        var roots = (isModel ? [] : ordinary).ToDictionary(static root => root.Name, StringComparer.OrdinalIgnoreCase);
        foreach (var group in outputScopes.GroupBy(static path => path.Trim('/').Split('/')[0], StringComparer.OrdinalIgnoreCase))
        {
            if (!project.AuthoringMounts.Any(mount => !IsDerivedRootMount(mount) && string.Equals(mount.Name, group.Key, StringComparison.OrdinalIgnoreCase)))
            {
                continue;
            }

            var root = roots.GetValueOrDefault(group.Key) ?? new(group.Key, publication.FindProjectRoot(group.Key), AssetUriHelper.GetVirtualPath(scope).Trim('/'), IsLocal: false);
            roots[group.Key] = root with { ImportedScopes = [.. group.Select(static path => path.Trim('/'))] };
        }

        return roots.Values.ToArray();
    }

    private async Task<CookedRootReport> InspectRootAsync(InspectionRoot root, CookProvenance provenance, bool validate, CancellationToken cancellationToken)
    {
        if (root.Path is null || !Directory.Exists(root.Path))
        {
            return new(root.Name, IsPresent: false, new(root.Path ?? string.Empty, Succeeded: true, SourceIdentity: null, [], [], []), Validation: null, []);
        }

        try
        {
            var lease = await CookOutputReadLease.AcquireAsync(root.Path, cancellationToken, requireIndex: false).ConfigureAwait(false);
            await using var lifetime = lease.ConfigureAwait(false);
            if (lease.GetFiles().Count == 0)
            {
                return new(root.Name, IsPresent: false, new(root.Path ?? string.Empty, Succeeded: true, SourceIdentity: null, [], [], []), Validation: null, []);
            }

            var actual = await lease.ReadInventoryAsync(this.engineContentPipelineApi, cancellationToken).ConfigureAwait(false);
            var inspection = actual.ToInspection(root.Path);
            var validation = validate ? actual.ToValidation(root.Path) : null;
            var known = root.IsLocal ? null : provenance.Roots.FirstOrDefault(item => string.Equals(item.Mount, root.Name, StringComparison.Ordinal));
            var origins = GetInspectionProvenance(root, provenance, actual);
            if (validation is not null && known is not null && !known.Matches(actual))
            {
                validation = validation with
                {
                    Succeeded = false,
                    Diagnostics = [.. validation.Diagnostics, new DiagnosticRecord
                {
                    OperationId = Guid.NewGuid(), Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                    Code = ContentPipelineDiagnosticCodes.ValidateFailed, Message = "The cooked index no longer matches its publication.",
                    AffectedPath = Path.Combine(root.Path, "container.index.bin"),
                }]
                };
            }

            var scoped = inspection with
            {
                Assets = inspection.Assets.Where(asset => IsInspectedAssetInScope(asset, root, origins)).ToArray(),
                Files = lease.GetFiles(),
            };
            return new(root.Name, IsPresent: true, scoped, validation, origins);
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException or System.Text.Json.JsonException or System.ComponentModel.Win32Exception)
        {
            var diagnostic = new DiagnosticRecord
            {
                OperationId = Guid.NewGuid(),
                Domain = FailureDomain.ContentPipeline,
                Severity = DiagnosticSeverity.Error,
                Code = ContentPipelineDiagnosticCodes.InspectFailed,
                Message = exception.Message,
                AffectedPath = root.Path,
                TechnicalMessage = exception.ToString(),
                ExceptionType = exception.GetType().FullName,
            };
            return new(root.Name, IsPresent: true, new(root.Path, Succeeded: false, SourceIdentity: null, [], [], [diagnostic]), Validation: null, []);
        }
    }

    private sealed record InspectionRoot(string Name, string? Path, string Scope, bool IsLocal)
    {
        public IReadOnlyList<string> ImportedScopes { get; init; } = [];
    }
}
