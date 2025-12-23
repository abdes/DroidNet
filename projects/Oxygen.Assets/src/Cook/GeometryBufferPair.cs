// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Assets.Cook;

[StructLayout(LayoutKind.Auto)]
public readonly record struct GeometryBufferPair(uint VertexBufferIndex, uint IndexBufferIndex);
