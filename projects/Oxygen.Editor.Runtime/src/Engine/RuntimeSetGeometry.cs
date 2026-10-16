// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to SetGeometry.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="AssetPath">The AssetPath command value.</param>
public sealed record RuntimeSetGeometry(NodeId NodeId, string AssetPath) : RuntimeWorldCommand;
