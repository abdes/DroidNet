// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Validates persisted discovery facts before any source path is opened.</summary>
internal sealed partial record CookProvenance
{
    /// <summary>Checks persisted source facts before their paths are opened.</summary>
    /// <param name="input">The persisted input facts.</param>
    /// <returns>Whether the input representation is valid.</returns>
    internal static bool ValidInputFacts(CookSnapshotInput input)
    {
        if ((!input.IsRootDirectoryProbe && !ValidRelativePath(input.RelativePath)) || !Enum.IsDefined(input.Kind)
            || input.DiscoveryHash is null || !ValidMetadata(input.Metadata))
        {
            return false;
        }

        return input.Kind switch
        {
            CookSnapshotInputKind.File => input.DiscoveryHash.Length == 64 && input.DiscoveryHash.All(Uri.IsHexDigit)
                && input.Metadata?.IsDirectory != true,
            CookSnapshotInputKind.Probe => input.DiscoveryHash.Length == 0,
            CookSnapshotInputKind.Absent => input.DiscoveryHash.Length == 0 && input.Metadata is null,
            _ => false,
        };
    }

    /// <summary>
    /// Creates the lookup of the accepted sources that produce each identity, among the sources still in the project; a
    /// source that was moved or deleted produces nothing any more. Identities differing only in case find each other.
    /// </summary>
    /// <param name="project">The project containing the sources.</param>
    /// <returns>Finds the source producing an identity, or null when no accepted source does.</returns>
    /// <exception cref="InvalidDataException">The accepted facts give an identity more than one source.</exception>
    internal Func<string, ContentCookInput?> CreateProducerLookup(Oxygen.Editor.Projects.ProjectContext project)
    {
        var producers = this.Products
            .Where(static product => product.SourceInput is not null)
            .Select(product => (product.DeclaredOutputs, Input: product.SourceInput! with
            {
                SourceAbsolutePath = project.ProjectRoot.Combine(product.SourceInput.SourceRelativePath),
                Role = ContentCookInputRole.Dependency,
            }))
            .Where(static product => File.Exists(product.Input.SourceAbsolutePath.FullPath))
            .SelectMany(static product => product.DeclaredOutputs.Select(output => (output.VirtualPath, product.Input)))
            .GroupBy(static item => item.VirtualPath, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(static group => group.Key, static group => group.Select(static item => item.Input).DistinctBy(static input => input.AssetUri).ToArray(), StringComparer.OrdinalIgnoreCase);
        return virtualPath => producers.GetValueOrDefault(virtualPath) switch
        {
            null => null,
            [var producer] => producer,
            _ => throw new InvalidDataException($"Multiple sources produce '{virtualPath}'."),
        };
    }

    private static bool ValidSourceFacts(Product product)
    {
        if (product.SourceFiles.IsDefault || product.DeclaredOutputs.IsDefault || product.NativeReferences.IsDefault
            || product.DeclaredOutputs.Concat(product.NativeReferences).Any(static item => item is null
                || !ValidVirtualPath(item.VirtualPath) || string.IsNullOrWhiteSpace(item.Kind) || item.ObjectPath is null))
        {
            return false;
        }

        // Every product has an authored source; engine assets come from the engine content pak, never a cook.
        if (product.SourceInput is not { } source)
        {
            return false;
        }

        if (source.AssetUri != product.SourceUri || !Enum.IsDefined(source.Kind) || !Enum.IsDefined(source.Role)
            || !ValidRelativePath(source.SourceRelativePath)
            || string.IsNullOrWhiteSpace(source.MountName)
            || product.SourceFiles.IsEmpty || product.DeclaredOutputs.IsEmpty)
        {
            return false;
        }

        var paths = new HashSet<string>(FileSystemPath.NameComparer);
        return product.SourceFiles.All(file => file is not null && paths.Add(file.RelativePath) && ValidInputFacts(file))
            && product.SourceFiles.Any(file => file.AssetUri == product.SourceUri
                && file.Kind == CookSnapshotInputKind.File && string.Equals(file.RelativePath, source.SourceRelativePath, FileSystemPath.NameComparison));
    }

    private static bool ValidMetadata(NativeSourceFileMetadata? metadata)
        => metadata is null || (metadata.LastModifiedNanoseconds is >= 0 and < 1_000_000_000
            && (!metadata.IsDirectory || metadata.Size == 0));

    private static bool ValidRelativePath(string? path)
        => !string.IsNullOrWhiteSpace(path) && !Path.IsPathRooted(path) && !path.Contains('\\')
            && path.Split('/').All(static segment => segment is not ("" or "." or "..")
                && segment.IndexOfAny(Path.GetInvalidFileNameChars()) < 0 && !segment.EndsWith('.') && !segment.EndsWith(' '));

    private static bool ValidVirtualPath(string? path)
        => path is { Length: > 1 } && path.StartsWith('/') && ValidRelativePath(path[1..]);
}
