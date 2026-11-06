// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Persistence.Pak;

namespace Oxygen.Managed.Assets.Persistence;

/// <summary>What one asset or texture of a library requires of another library.</summary>
/// <param name="IsTexture">Whether the requirement names a texture rather than an asset.</param>
/// <param name="Key">The key of the required asset or texture.</param>
/// <param name="AssetType">The engine asset type of the required asset; zero for a texture.</param>
public sealed record LibraryRequirement(bool IsTexture, AssetKey Key, byte AssetType);

/// <summary>What a library requires of one other library.</summary>
/// <param name="Library">The required library's mount name.</param>
/// <param name="Requirements">The requirements.</param>
public sealed record LibraryDependency(string Library, IReadOnlyList<LibraryRequirement> Requirements);

/// <summary>One asset a library provides.</summary>
/// <param name="VirtualPath">The asset's virtual path.</param>
/// <param name="Key">The asset's key.</param>
/// <param name="AssetType">The engine asset type.</param>
/// <param name="DescriptorRelativePath">The descriptor's path below a loose root; <see langword="null"/> in a pak.</param>
public sealed record LibraryAsset(string VirtualPath, AssetKey Key, byte AssetType, string? DescriptorRelativePath);

/// <summary>
/// What a cooked library holds, read from a loose root's index or a pak's header, footer, browse index and catalog
/// without loading its assets. It is the managed twin of the engine's cooked summary, so the editor and the engine
/// decide alike whether a mount can use the library.
/// </summary>
public sealed record LibraryFacts
{
    private const int ScriptExternalSourceOffset = 58;

    /// <summary>Gets the mounts its virtual paths start with, sorted. A valid library has one.</summary>
    public required IReadOnlyList<string> Roots { get; init; }

    /// <summary>Gets the virtual paths of its scripts whose source is external, sorted. A pak has none: PakTool seals scripts.</summary>
    public IReadOnlyList<string> ExternalScripts { get; init; } = [];

    /// <summary>Gets what it requires of other libraries.</summary>
    public IReadOnlyList<LibraryDependency> Dependencies { get; init; } = [];

    /// <summary>Gets the assets it provides.</summary>
    public IReadOnlyList<LibraryAsset> Assets { get; init; } = [];

    /// <summary>Gets the virtual paths of the textures it provides, with the path of a loose root's descriptor.</summary>
    public IReadOnlyDictionary<string, string?> Textures { get; init; } = new Dictionary<string, string?>();

    /// <summary>Gets the lower assets a pak deletes, which nothing below it then provides.</summary>
    public IReadOnlySet<AssetKey> Deleted { get; init; } = new HashSet<AssetKey>();

    /// <summary>Reads the facts of a pak.</summary>
    /// <param name="document">The pak's metadata.</param>
    /// <returns>The facts.</returns>
    public static LibraryFacts Of(PakDocument document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return new LibraryFacts
        {
            Roots = [document.Library],
            Dependencies = document.Dependencies,
            Assets = [.. document.Assets.Select(static asset => new LibraryAsset(asset.VirtualPath, asset.AssetKey, asset.AssetType, null))],
            Textures = document.Textures.ToDictionary(static texture => texture.VirtualPath, static _ => (string?)null, StringComparer.Ordinal),
            Deleted = document.Deleted.ToHashSet(),
        };
    }

    /// <summary>Reads the facts of a loose cooked root.</summary>
    /// <param name="index">The root's index.</param>
    /// <param name="readDescriptor">Reads a descriptor by its path below the root; only scripts' descriptors are read.</param>
    /// <returns>The facts.</returns>
    /// <exception cref="InvalidDataException">A script's descriptor is too short to hold its external source.</exception>
    public static LibraryFacts Of(Document index, Func<string, byte[]> readDescriptor)
    {
        ArgumentNullException.ThrowIfNull(index);
        ArgumentNullException.ThrowIfNull(readDescriptor);
        var roots = new SortedSet<string>(StringComparer.Ordinal);
        var scripts = new SortedSet<string>(StringComparer.Ordinal);
        var assets = new List<LibraryAsset>();
        foreach (var asset in index.Assets.Where(static asset => asset.VirtualPath is not null))
        {
            _ = roots.Add(RootOf(asset.VirtualPath!));
            assets.Add(new(asset.VirtualPath!, asset.AssetKey, asset.AssetType, asset.DescriptorRelativePath));
            if (asset.AssetType == (byte)AssetType.Script && HasExternalSource(readDescriptor(asset.DescriptorRelativePath)))
            {
                _ = scripts.Add(asset.VirtualPath!);
            }
        }

        var textures = new Dictionary<string, string?>(StringComparer.Ordinal);
        foreach (var file in index.Files.Where(static file => file.VirtualPath is not null))
        {
            _ = roots.Add(RootOf(file.VirtualPath!));
            if (file.Kind == FileKind.TextureDescriptor)
            {
                textures[file.VirtualPath!] = file.RelativePath;
            }
        }

        return new LibraryFacts
        {
            Roots = [.. roots],
            ExternalScripts = [.. scripts],
            Dependencies = DependenciesOf(index),
            Assets = assets,
            Textures = textures,
        };
    }

    /// <summary>Tests whether the library provides an asset, and of which type.</summary>
    /// <param name="key">The asset's key.</param>
    /// <param name="assetType">The asset's type, when it does.</param>
    /// <returns><see langword="true"/> when it provides the asset.</returns>
    public bool TryFindAsset(AssetKey key, out byte assetType)
    {
        var asset = this.Assets.FirstOrDefault(asset => asset.Key == key);
        assetType = asset?.AssetType ?? 0;
        return asset is not null;
    }

    /// <summary>Tests whether the library provides a texture.</summary>
    /// <param name="key">The texture's key, from its virtual path.</param>
    /// <returns><see langword="true"/> when it provides the texture.</returns>
    public bool HasTexture(AssetKey key) => this.Textures.Keys.Any(path => AssetKey.FromVirtualPath(path) == key);

    /// <summary>Tests whether the library holds a virtual path as an asset or a texture.</summary>
    /// <param name="virtualPath">The virtual path.</param>
    /// <returns><see langword="true"/> when it holds the path.</returns>
    public bool Holds(string virtualPath)
        => this.Textures.ContainsKey(virtualPath) || this.Assets.Any(asset => string.Equals(asset.VirtualPath, virtualPath, StringComparison.Ordinal));

    /// <summary>Gets the path below a loose root of the descriptor of a virtual path.</summary>
    /// <param name="virtualPath">The virtual path.</param>
    /// <returns>The descriptor's path; <see langword="null"/> in a pak, or when the library does not hold the path.</returns>
    public string? DescriptorOf(string virtualPath)
        => this.Textures.TryGetValue(virtualPath, out var texture)
            ? texture
            : this.Assets.FirstOrDefault(asset => string.Equals(asset.VirtualPath, virtualPath, StringComparison.Ordinal))?.DescriptorRelativePath;

    /// <summary>Gets the virtual path of a descriptor below a loose root.</summary>
    /// <param name="relativePath">The descriptor's path below the root, with <c>/</c> separators.</param>
    /// <returns>The virtual path, or <see langword="null"/> when no asset or texture has the descriptor.</returns>
    public string? VirtualPathOfDescriptor(string relativePath)
        => this.Assets.FirstOrDefault(asset => string.Equals(asset.DescriptorRelativePath, relativePath, StringComparison.OrdinalIgnoreCase))?.VirtualPath
            ?? this.Textures.FirstOrDefault(texture => string.Equals(texture.Value, relativePath, StringComparison.OrdinalIgnoreCase)).Key;

    private static string RootOf(string virtualPath)
    {
        var end = virtualPath.IndexOf('/', 1);
        return end < 0 ? virtualPath[1..] : virtualPath[1..end];
    }

    private static bool HasExternalSource(byte[] descriptor)
        => descriptor.Length < ScriptExternalSourceOffset + sizeof(uint)
            ? throw new InvalidDataException("A script's descriptor is too short.")
            : BinaryPrimitives.ReadUInt32LittleEndian(descriptor.AsSpan(ScriptExternalSourceOffset)) != 0;

    private static List<LibraryDependency> DependenciesOf(Document index)
        => [.. index.Requirements
            .GroupBy(static requirement => requirement.Mount, StringComparer.Ordinal)
            .OrderBy(static group => group.Key, StringComparer.Ordinal)
            .Select(static group => new LibraryDependency(
                group.Key,
                [.. group.Select(static requirement => new LibraryRequirement(requirement.IsTexture, requirement.Key, requirement.AssetType)).Distinct()]))];
}
