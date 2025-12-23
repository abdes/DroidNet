// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Import;

/// <summary>
/// Placeholder identity policy that derives <see cref="AssetKey"/> deterministically from virtual paths.
/// </summary>
/// <remarks>
/// This is intended only as an MVP stub until sidecar-backed identity is implemented.
/// </remarks>
public sealed class DeterministicVirtualPathIdentityPolicy : IAssetIdentityPolicy
{
    /// <inheritdoc />
    public AssetKey GetOrCreateAssetKey(string virtualPath, string assetType)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(virtualPath);
        _ = assetType;

        var bytes = Encoding.UTF8.GetBytes(virtualPath);
        Span<byte> hash = stackalloc byte[32];
        _ = SHA256.HashData(bytes, hash);

        var part0 = BinaryPrimitives.ReadUInt64LittleEndian(hash[..8]);
        var part1 = BinaryPrimitives.ReadUInt64LittleEndian(hash.Slice(8, 8));
        return new AssetKey(part0, part1);
    }
}
