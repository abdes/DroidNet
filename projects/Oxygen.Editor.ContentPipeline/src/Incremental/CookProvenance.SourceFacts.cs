// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Validates persisted discovery facts before any source path is opened.</summary>
internal sealed partial record CookProvenance
{
    /// <summary>Checks persisted source facts before their paths are opened.</summary>
    /// <param name="input">The persisted input facts.</param>
    /// <returns>Whether the input representation is valid.</returns>
    internal static bool ValidInputFacts(CookSnapshotInput input)
    {
        if ((!input.IsRootDirectoryProbe && !ValidRelativePath(input.RelativePath)) || !Path.IsPathFullyQualified(input.SourcePath)
            || input.AssetUri is { IsAbsoluteUri: false } || !Enum.IsDefined(input.Kind)
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

    /// <summary>Finds the accepted source that declares a buffer output.</summary>
    /// <param name="project">The project containing the source.</param>
    /// <param name="virtualPath">The native buffer identity.</param>
    /// <returns>The source input, or null when no accepted source declares it.</returns>
    internal ContentCookInput? FindBufferOwner(Oxygen.Editor.Projects.ProjectContext project, string virtualPath)
    {
        var owners = this.Products.Where(product => product.SourceInput is not null && product.DeclaredOutputs.Any(output =>
            string.Equals(output.Kind, "buffer", StringComparison.Ordinal) && string.Equals(output.VirtualPath, virtualPath, StringComparison.Ordinal))).ToArray();
        if (owners.Length > 1)
        {
            throw new InvalidDataException($"Multiple sources claim buffer '{virtualPath}'.");
        }

        return owners.FirstOrDefault()?.SourceInput is { } input ? input with
        {
            SourceAbsolutePath = Path.GetFullPath(Path.Combine(project.ProjectRoot, input.SourceRelativePath)),
            Role = ContentCookInputRole.Dependency,
        } : null;
    }

    private static bool ValidSourceFacts(Product product)
    {
        if (product.SourceFiles.IsDefault || product.DeclaredOutputs.IsDefault || product.NativeReferences.IsDefault
            || product.DeclaredOutputs.Concat(product.NativeReferences).Any(static item => item is null
                || !ValidVirtualPath(item.VirtualPath) || string.IsNullOrWhiteSpace(item.Kind) || item.ObjectPath is null))
        {
            return false;
        }

        if (product.SourceInput is not { } source)
        {
            return product.SourceFiles.IsEmpty && product.DeclaredOutputs.IsEmpty && product.NativeReferences.IsEmpty
                && product.SourceUri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase);
        }

        if (source.AssetUri != product.SourceUri || !Enum.IsDefined(source.Kind) || !Enum.IsDefined(source.Role)
            || !ValidRelativePath(source.SourceRelativePath) || !Path.IsPathFullyQualified(source.SourceAbsolutePath)
            || string.IsNullOrWhiteSpace(source.MountName) || source.OutputNamespaces.IsDefault
            || (source.OutputVirtualPath is { } output && !ValidVirtualPath(output))
            || product.SourceFiles.IsEmpty || product.DeclaredOutputs.IsEmpty)
        {
            return false;
        }

        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        return product.SourceFiles.All(file => file is not null && paths.Add(file.RelativePath) && ValidInputFacts(file))
            && product.SourceFiles.Any(file => file.AssetUri == product.SourceUri
                && file.Kind == CookSnapshotInputKind.File && string.Equals(file.RelativePath, source.SourceRelativePath, StringComparison.OrdinalIgnoreCase));
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
