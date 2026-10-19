// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Resolves imported outputs to retained work without inventing native asset identities.</summary>
internal sealed class ImportedSourceIndex
{
    private readonly Dictionary<AssetPath, AssetPath> previousOwners = [];
    private readonly List<Entry> entries = [];
    private string? projectRoot;

    /// <summary>Gets previously identified native outputs for project key lookup.</summary>
    public IReadOnlyCollection<AssetPath> KnownOutputs => this.previousOwners.Keys;

    /// <summary>Reads saved source namespaces and trusted prior output ownership once for a cook.</summary>
    /// <param name="project">The project's authoring mount declarations.</param>
    /// <param name="documents">Owners protecting acknowledged saved settings.</param>
    /// <param name="previous">Verified prior output ownership.</param>
    /// <param name="cancellationToken">Cancels settings discovery.</param>
    /// <returns>The source namespaces and prior named outputs.</returns>
    public static Task<ImportedSourceIndex> ReadAsync(ProjectContext project, ICookDocumentRegistry documents, CookProvenance previous, CancellationToken cancellationToken)
        => ReadAsync(project, (path, token) => CookSavedSourceReader.ReadAsync(documents, path, token, allowUnsavedDocuments: true), previous, cancellationToken);

    /// <summary>Reads source namespaces and trusted prior output ownership with the given settings reader.</summary>
    /// <param name="project">The project's authoring mount declarations.</param>
    /// <param name="readSettings">Reads one import settings file: a cook reads what open documents acknowledge, a status reads the disk.</param>
    /// <param name="previous">Verified prior output ownership.</param>
    /// <param name="cancellationToken">Cancels settings discovery.</param>
    /// <returns>The source namespaces and prior named outputs.</returns>
    public static async Task<ImportedSourceIndex> ReadAsync(ProjectContext project, Func<string, CancellationToken, Task<byte[]>> readSettings, CookProvenance previous, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(readSettings);
        var index = new ImportedSourceIndex();
        foreach (var mount in project.AuthoringMounts)
        {
            var root = project.ProjectRoot.Combine(mount.RelativePath).FullPath;
            var relative = Path.GetRelativePath(project.ProjectRoot.FullPath, root).Replace('\\', '/');
            if (Path.IsPathRooted(relative) || relative.StartsWith("..", StringComparison.Ordinal)
                || ProjectOutputFolders.Contains(relative) || !Directory.Exists(root))
            {
                continue;
            }

            var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = false, MatchCasing = MatchCasing.CaseInsensitive };
            foreach (var path in Directory.EnumerateFiles(root, "*" + NativeSceneImportSettings.SidecarSuffix, options))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var primary = path[..^NativeSceneImportSettings.SidecarSuffix.Length];
                if (!AssetFileTypes.Is(primary, AssetFileCategory.Model))
                {
                    continue;
                }

                try
                {
                    var settings = NativeSceneImportSettings.Parse(await readSettings(path, cancellationToken).ConfigureAwait(false));
                    if (FileSystemPath.From(settings.ResolveFile(project.ProjectRoot.FullPath, settings.PrimaryRelativePath)) == FileSystemPath.From(primary)
                        && project.AuthoringMounts.Any(item => string.Equals(item.Name, settings.MountPoint, StringComparison.Ordinal))
                        && AssetPath.TryCreate(mount.Name, FileSystemPath.From(primary).GetPathBelow(FileSystemPath.From(root)), out var sourceUri))
                    {
                        index.entries.AddRange(settings.OutputPrefixes.Select(prefix => new Entry(prefix, sourceUri)));
                    }
                }
                catch (Exception failure) when (failure is InvalidDataException or System.Text.Json.JsonException or FileNotFoundException)
                {
                    // Invalid settings cannot claim a namespace. A requested source still reports its own validation failure.
                }
            }
        }

        // A moved or deleted model, or an output its sidecar's group no longer covers, keeps no ownership:
        // the relocation rewrote every reference to it, and the next cook retires it.
        index.projectRoot = project.ProjectRoot.FullPath;
        foreach (var product in previous.Products.Where(static product => product.ImportedSource is not null))
        {
            if (Incremental.CookRootRepair.IsRetired(project.ProjectRoot.FullPath, product))
            {
                continue;
            }

            foreach (var output in product.Outputs.Where(output => !index.IsOutsideCurrentGroup(product.SourceUri, output.Asset.CookedAssetUri)))
            {
                index.previousOwners.Add(output.Asset.CookedAssetUri, product.SourceUri);
            }
        }

        return index;
    }

    /// <summary>Tests whether an imported product's outputs left the group its saved settings now declare.</summary>
    /// <param name="product">A prior cooked product.</param>
    /// <returns>Whether the product must be rebuilt into its new group.</returns>
    public bool IsRelocated(Incremental.CookProvenance.Product product)
    {
        ArgumentNullException.ThrowIfNull(product);
        return product.ImportedSource is not null && this.projectRoot is { } root && !Incremental.CookRootRepair.IsRetired(root, product)
            && product.Outputs.Any(output => this.IsOutsideCurrentGroup(product.SourceUri, output.Asset.CookedAssetUri));
    }

    /// <summary>Finds the source that must produce a native output; existence is checked after cooking.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="uri">The requested native identity.</param>
    /// <param name="role">The source's role in the closure.</param>
    /// <returns>The retained input, or null for an output without an imported owner.</returns>
    public ContentCookInput? ResolveOutput(ProjectContext project, AssetPath uri, ContentCookInputRole role)
    {
        var resolution = this.ResolveOutputFacts(project, uri, role);
        return resolution.Error is { } error ? throw new InvalidDataException(error) : resolution.Source;
    }

    /// <summary>Reads source ownership for status without throwing for ordinary namespace conflicts.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="uri">The requested native identity.</param>
    /// <param name="role">The source's role in the closure.</param>
    /// <returns>The source, a conflict, or an unowned output.</returns>
    public Resolution ResolveOutputFacts(ProjectContext project, AssetPath uri, ContentCookInputRole role)
    {
        if (!AssetFileTypes.IsProductOf(uri.Name, AssetFileCategory.Model))
        {
            return new(Source: null, Error: null);
        }

        var path = uri.VirtualPath;
        var owners = this.entries.Where(entry => path.StartsWith(entry.Prefix, StringComparison.Ordinal)).Select(static entry => entry.Source).Distinct().ToArray();
        _ = this.previousOwners.TryGetValue(uri, out var previous);
        if (owners.Length > 1 || (owners.Length == 1 && previous is not null && previous != owners[0]))
        {
            return new(Source: null, $"Import destination for '{uri}' overlaps another retained source. Choose separate import destinations.");
        }

        var source = previous ?? owners.FirstOrDefault();
        if (source is null)
        {
            return new(Source: null, Error: null);
        }

        var descriptor = CookInputResolver.Resolve(project, uri, role);
        return File.Exists(descriptor.SourceAbsolutePath.FullPath)
            ? new(Source: null, $"Imported output '{uri}' conflicts with authored descriptor '{descriptor.AssetUri}'. Keep separate output destinations.")
            : new(CookInputResolver.Resolve(project, source, role), Error: null);
    }

    /// <summary>Finds declared and recorded native output for a source inspection scope.</summary>
    /// <param name="scope">The source identity or source folder to inspect.</param>
    /// <returns>Declared output namespaces and previously published outputs for matching sources.</returns>
    public IEnumerable<string> GetInspectionOutputScopes(AssetPath scope)
    {
        bool Matches(AssetPath source) => source.IsSameOrInside(scope);
        return this.entries.Where(entry => Matches(entry.Source)).Select(static entry => entry.Prefix.TrimEnd('/'))
            .Concat(this.previousOwners.Where(pair => Matches(pair.Value)).Select(static pair => pair.Key.VirtualPath))
            .Distinct(StringComparer.Ordinal);
    }

    /// <summary>Includes retained work when the selected folder contains native output rather than descriptors.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="folder">The requested virtual folder.</param>
    /// <returns>The distinct source owners whose output intersects the folder.</returns>
    public IEnumerable<ContentCookInput> ResolveFolder(ProjectContext project, AssetPath folder)
    {
        var prefix = folder.VirtualPath.TrimEnd('/') + "/";
        var sources = this.entries.Where(entry => entry.Prefix.StartsWith(prefix, StringComparison.Ordinal) || prefix.StartsWith(entry.Prefix, StringComparison.Ordinal))
            .Select(static entry => entry.Source)
            .Concat(this.previousOwners.Where(pair => pair.Key.VirtualPath.StartsWith(prefix, StringComparison.Ordinal)).Select(static pair => pair.Value));
        return sources.Distinct().Select(uri => CookInputResolver.Resolve(project, uri, ContentCookInputRole.Primary));
    }

    /// <summary>Preserves the requested output while substituting its retained source in capture and freshness checks.</summary>
    /// <param name="scope">The original requested scope.</param>
    /// <returns>The scope with source owners and exact required output identities.</returns>
    public ContentCookScope ResolveScope(ContentCookScope scope)
    {
        foreach (var input in scope.Inputs.Where(static input => input.Kind != ContentCookAssetKind.ForeignSource && input.OutputVirtualPath is not null))
        {
            _ = this.ResolveOutput(scope.Project, AssetPath.Parse(input.OutputVirtualPath!), input.Role);
        }

        return scope.ScopeUri is not { } requested ? scope
            : scope.TargetKind == CookTargetKind.Folder
            ? scope with { Inputs = scope.Inputs.Concat(this.ResolveFolder(scope.Project, requested)).DistinctBy(static input => input.AssetUri).ToArray() }
            : this.ResolveOutput(scope.Project, requested, ContentCookInputRole.Primary) is { } source
            ? scope with { Inputs = [source], RequiredImportedOutputs = [requested] } : scope;
    }

    private bool IsOutsideCurrentGroup(AssetPath source, AssetPath output)
    {
        var prefixes = this.entries.Where(entry => entry.Source == source).Select(static entry => entry.Prefix).ToArray();
        var path = output.VirtualPath;
        return prefixes.Length != 0 && !prefixes.Any(prefix => path.StartsWith(prefix, StringComparison.Ordinal));
    }

    /// <summary>One ownership lookup, including a named conflict without claiming an output exists.</summary>
    /// <param name="Source">The retained input, if unambiguously owned.</param>
    /// <param name="Error">The conflict requiring user correction.</param>
    public sealed record Resolution(ContentCookInput? Source, string? Error);

    private sealed record Entry(string Prefix, AssetPath Source);
}
