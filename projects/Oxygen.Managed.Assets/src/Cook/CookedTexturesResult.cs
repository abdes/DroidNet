// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Managed.Assets.Cook;

[StructLayout(LayoutKind.Auto)]
public readonly record struct CookedTexturesResult(
    ReadOnlyMemory<byte> TableBytes,
    ReadOnlyMemory<byte> DataBytes,
    IReadOnlyDictionary<AssetKey, uint> Indices);
