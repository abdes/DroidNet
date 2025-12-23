// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Cook;

public sealed record CookedBuffersResult(
    ReadOnlyMemory<byte> BuffersTableBytes,
    ReadOnlyMemory<byte> BuffersDataBytes,
    IReadOnlyDictionary<AssetKey, GeometryBufferPair> BufferIndices);
