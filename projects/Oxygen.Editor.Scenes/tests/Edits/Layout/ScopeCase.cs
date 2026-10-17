// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>A layout place and the scene scope it is in.</summary>
/// <param name="Position">The place.</param>
/// <param name="Scope">The node whose children the place groups, or <see langword="null"/> for the root.</param>
public sealed record ScopeCase(LayoutPosition Position, NodeId? Scope);
