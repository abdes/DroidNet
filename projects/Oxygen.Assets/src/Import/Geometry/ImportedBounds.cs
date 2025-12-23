// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Runtime.InteropServices;

namespace Oxygen.Assets.Import.Geometry;

[StructLayout(LayoutKind.Sequential)]
public readonly record struct ImportedBounds(Vector3 Min, Vector3 Max);
