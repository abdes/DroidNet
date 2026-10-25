// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>A node continuity entry: the node with <paramref name="Anchor"/> keeps the ID of the node with
/// <paramref name="Continues"/>, as when the artist renamed or moved it in the DCC tool.</summary>
/// <param name="Anchor">The node's anchor now (<c>Body/Door_Front_L</c>).</param>
/// <param name="Continues">The anchor whose ID it keeps (<c>Body/Door_FL</c>).</param>
public sealed record ModelNodeContinuity(string Anchor, string Continues);
