// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>A node the last import of a model emitted.</summary>
/// <param name="Anchor">The node's anchor, its path of names in the source file.</param>
/// <param name="NodeId">The node's ID.</param>
public sealed record ModelPublishedNode(string Anchor, Guid NodeId);
