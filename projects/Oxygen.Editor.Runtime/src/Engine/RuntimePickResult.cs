// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The nodes a viewport pick found.</summary>
/// <param name="Hits">One entry per node, closest to the rectangle centre first, then nearest.</param>
/// <param name="WorldPosition">World position under the first hit, if any.</param>
public sealed record RuntimePickResult(IReadOnlyList<RuntimePickHit> Hits, Vector3? WorldPosition);
