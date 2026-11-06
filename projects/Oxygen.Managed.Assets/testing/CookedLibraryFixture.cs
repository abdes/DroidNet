// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Assets.Persistence.Publication;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>Builds the bytes of cooked libraries for tests of what binds them; never shipped.</summary>
public static class CookedLibraryFixture
{
    /// <summary>Encodes the index of a loose root holding materials at virtual paths, each with a content-named descriptor.</summary>
    /// <param name="virtualPaths">The assets' virtual paths.</param>
    /// <returns>The index's bytes.</returns>
    public static byte[] LooseIndex(params string[] virtualPaths)
    {
        ArgumentNullException.ThrowIfNull(virtualPaths);
        using var stream = new MemoryStream();
        LooseCookedIndexFixture.Write(stream, new Document(1, default, Guid.CreateVersion7(), [.. virtualPaths.Select(static path => new AssetEntry(AssetKey.FromVirtualPath(path), DescriptorOf(path), path, (byte)AssetType.Material, 4, SHA256.HashData(Encoding.UTF8.GetBytes(path))))], []));
        return stream.ToArray();
    }

    /// <summary>Gets the path below a fixture's loose root of a virtual path's descriptor.</summary>
    /// <param name="virtualPath">The virtual path.</param>
    /// <returns>The path, with <c>/</c> separators.</returns>
    public static string DescriptorOf(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        return virtualPath[(virtualPath.IndexOf('/', 1) + 1)..] + ".0123456789abcdef";
    }

    /// <summary>Encodes a pak holding materials at virtual paths.</summary>
    /// <param name="library">The pak's one root name.</param>
    /// <param name="virtualPaths">The assets' virtual paths.</param>
    /// <returns>The pak's bytes.</returns>
    public static byte[] Pak(string library, params string[] virtualPaths)
    {
        ArgumentNullException.ThrowIfNull(virtualPaths);
        using var stream = new MemoryStream();
        PakFixture.Write(stream, new PakDocument(
            1,
            Guid.CreateVersion7(),
            new string('0', 64),
            [.. virtualPaths.Select(static path => new PakAssetEntry(AssetKey.FromVirtualPath(path), (byte)AssetType.Material, path, 0, 0))])
        {
            Library = library,
        });
        return stream.ToArray();
    }

    /// <summary>Encodes what a library project's <c>.cooked</c> folder holds to select a generation for each of its roots.</summary>
    /// <param name="roots">Each root's name and generation.</param>
    /// <returns>The files below <c>.cooked</c> that select a generation: paths with <c>/</c> separators and their bytes.</returns>
    public static IReadOnlyDictionary<string, byte[]> Publication(params (string Root, Guid SourceKey)[] roots)
    {
        ArgumentNullException.ThrowIfNull(roots);
        var document = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new
        {
            version = CookPublicationFormat.DocumentVersion,
            roots = roots.Select(static root => new { owner = 0, name = root.Root, sourceKey = root.SourceKey }),
        }));
        var publication = Guid.CreateVersion7();
        var head = JsonSerializer.SerializeToUtf8Bytes(new CookPublicationHead(CookPublicationHead.CurrentVersion, publication, Convert.ToHexString(SHA256.HashData(document))), new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase });
        return new Dictionary<string, byte[]>(StringComparer.Ordinal)
        {
            [CookPublicationFormat.HeadFileName] = head,
            [CookPublicationFormat.DocumentPath(publication)] = document,
        };
    }
}
