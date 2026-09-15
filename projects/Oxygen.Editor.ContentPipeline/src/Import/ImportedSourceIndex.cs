// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Resolves imported outputs to retained work without inventing native asset identities.</summary>
internal sealed class ImportedSourceIndex
{
    private readonly Dictionary<Uri, Uri> previousOwners = [];
    private readonly List<Entry> entries = [];

    /// <summary>Gets previously identified native outputs for project key lookup.</summary>
    public IReadOnlyCollection<Uri> KnownOutputs => this.previousOwners.Keys;

    /// <summary>Reads saved source namespaces and trusted prior output ownership once for a request.</summary>
    /// <param name="project">The project's authoring mount declarations.</param>
    /// <param name="documents">Owners protecting acknowledged saved settings.</param>
    /// <param name="previous">Verified prior output ownership.</param>
    /// <param name="cancellationToken">Cancels settings discovery.</param>
    /// <returns>The source namespaces and prior named outputs.</returns>
    public static async Task<ImportedSourceIndex> ReadAsync(ProjectContext project, ICookDocumentRegistry documents, CookProvenance previous, CancellationToken cancellationToken)
    {
        var index = new ImportedSourceIndex();
        foreach (var product in previous.Products.Where(static product => product.ImportedSource is not null))
        {
            foreach (var output in product.Outputs)
            {
                index.previousOwners.Add(output.Asset.CookedAssetUri, product.SourceUri);
            }
        }

        foreach (var mount in project.AuthoringMounts)
        {
            var root = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
            var relative = Path.GetRelativePath(project.ProjectRoot, root).Replace('\\', '/');
            if (Path.IsPathRooted(relative) || relative.StartsWith("..", StringComparison.Ordinal)
                || relative.Split('/')[0].ToUpperInvariant() is ".COOKED" or ".IMPORTED" or ".BUILD" or ".PIPELINE" || !Directory.Exists(root))
            {
                continue;
            }

            var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = false, MatchCasing = MatchCasing.CaseInsensitive };
            foreach (var path in Directory.EnumerateFiles(root, "*" + NativeSceneImportSettings.SidecarSuffix, options))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var primary = path[..^NativeSceneImportSettings.SidecarSuffix.Length];
                if (Path.GetExtension(primary).ToUpperInvariant() is not (".GLTF" or ".GLB" or ".FBX"))
                {
                    continue;
                }

                try
                {
                    var settings = NativeSceneImportSettings.Parse(await CookSavedSourceReader.ReadAsync(documents, path, cancellationToken, allowUnsavedDocuments: true).ConfigureAwait(false));
                    if (string.Equals(settings.ResolveFile(project.ProjectRoot, settings.PrimaryRelativePath), primary, StringComparison.OrdinalIgnoreCase)
                        && project.AuthoringMounts.Any(item => string.Equals(item.Name, settings.MountPoint, StringComparison.OrdinalIgnoreCase)))
                    {
                        var sourceRelative = Path.GetRelativePath(root, primary).Replace('\\', '/');
                        var sourceUri = new Uri(AssetUris.Scheme + ":///" + Uri.EscapeDataString(mount.Name) + "/" + string.Join('/', sourceRelative.Split('/').Select(Uri.EscapeDataString)));
                        index.entries.AddRange(settings.OutputPrefixes.Select(prefix => new Entry(prefix, sourceUri)));
                    }
                }
                catch (Exception failure) when (failure is InvalidDataException or System.Text.Json.JsonException or FileNotFoundException)
                {
                    // Invalid settings cannot claim a namespace. A requested source still reports its own validation failure.
                }
            }
        }

        return index;
    }

    /// <summary>Finds the source that must produce a native output; existence is checked after cooking.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="uri">The requested native identity.</param>
    /// <param name="role">The source's role in the closure.</param>
    /// <returns>The retained input, or null for an output without an imported owner.</returns>
    public ContentCookInput? ResolveOutput(ProjectContext project, Uri uri, ContentCookInputRole role)
    {
        var resolution = this.ResolveOutputFacts(project, uri, role);
        return resolution.Error is { } error ? throw new InvalidDataException(error) : resolution.Source;
    }

    /// <summary>Reads source ownership for status without throwing for ordinary namespace conflicts.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="uri">The requested native identity.</param>
    /// <param name="role">The source's role in the closure.</param>
    /// <returns>The source, a conflict, or an unowned output.</returns>
    public Resolution ResolveOutputFacts(ProjectContext project, Uri uri, ContentCookInputRole role)
    {
        if (!string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            || Path.GetExtension(uri.AbsolutePath).ToUpperInvariant() is not (".OGEO" or ".OMAT" or ".OSCENE"))
        {
            return new(Source: null, Error: null);
        }

        var path = Uri.UnescapeDataString(uri.AbsolutePath);
        var owners = this.entries.Where(entry => path.StartsWith(entry.Prefix, StringComparison.OrdinalIgnoreCase)).Select(static entry => entry.Source).Distinct().ToArray();
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
        return File.Exists(descriptor.SourceAbsolutePath)
            ? new(Source: null, $"Imported output '{uri}' conflicts with authored descriptor '{descriptor.AssetUri}'. Keep separate output destinations.")
            : new(CookInputResolver.Resolve(project, source, role), Error: null);
    }

    /// <summary>Finds declared and recorded native output for a source inspection scope.</summary>
    /// <param name="scope">The source identity or source folder to inspect.</param>
    /// <returns>Declared output namespaces and previously published outputs for matching sources.</returns>
    public IEnumerable<string> GetInspectionOutputScopes(Uri scope)
    {
        var path = Uri.UnescapeDataString(scope.AbsolutePath).TrimEnd('/');
        bool Matches(Uri source) => string.Equals(Uri.UnescapeDataString(source.AbsolutePath), path, StringComparison.OrdinalIgnoreCase)
            || Uri.UnescapeDataString(source.AbsolutePath).StartsWith(path + "/", StringComparison.OrdinalIgnoreCase);
        return this.entries.Where(entry => Matches(entry.Source)).Select(static entry => entry.Prefix.TrimEnd('/'))
            .Concat(this.previousOwners.Where(pair => Matches(pair.Value)).Select(static pair => Uri.UnescapeDataString(pair.Key.AbsolutePath)))
            .Distinct(StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>Includes retained work when the selected folder contains native output rather than descriptors.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="folder">The requested virtual folder.</param>
    /// <returns>The distinct source owners whose output intersects the folder.</returns>
    public IEnumerable<ContentCookInput> ResolveFolder(ProjectContext project, Uri folder)
    {
        var prefix = Uri.UnescapeDataString(folder.AbsolutePath).TrimEnd('/') + "/";
        var sources = this.entries.Where(entry => entry.Prefix.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) || prefix.StartsWith(entry.Prefix, StringComparison.OrdinalIgnoreCase))
            .Select(static entry => entry.Source)
            .Concat(this.previousOwners.Where(pair => Uri.UnescapeDataString(pair.Key.AbsolutePath).StartsWith(prefix, StringComparison.OrdinalIgnoreCase)).Select(static pair => pair.Value));
        return sources.Distinct().Select(uri => CookInputResolver.Resolve(project, uri, ContentCookInputRole.Primary));
    }

    /// <summary>Preserves the requested output while substituting its retained source in capture and freshness checks.</summary>
    /// <param name="scope">The original requested scope.</param>
    /// <returns>The scope with source owners and exact required output identities.</returns>
    public ContentCookScope ResolveScope(ContentCookScope scope)
    {
        foreach (var input in scope.Inputs.Where(static input => input.Kind != ContentCookAssetKind.ForeignSource && input.OutputVirtualPath is not null))
        {
            _ = this.ResolveOutput(scope.Project, new Uri(AssetUris.Scheme + "://" + input.OutputVirtualPath), input.Role);
        }

        return scope.ScopeUri is not { } requested ? scope
            : scope.TargetKind == CookTargetKind.Folder
            ? scope with { Inputs = scope.Inputs.Concat(this.ResolveFolder(scope.Project, requested)).DistinctBy(static input => input.AssetUri).ToArray() }
            : this.ResolveOutput(scope.Project, requested, ContentCookInputRole.Primary) is { } source
            ? scope with { Inputs = [source], RequiredImportedOutputs = [requested] } : scope;
    }

    /// <summary>One ownership lookup, including a named conflict without claiming an output exists.</summary>
    /// <param name="Source">The retained input, if unambiguously owned.</param>
    /// <param name="Error">The conflict requiring user correction.</param>
    public sealed record Resolution(ContentCookInput? Source, string? Error);

    private sealed record Entry(string Prefix, Uri Source);
}
