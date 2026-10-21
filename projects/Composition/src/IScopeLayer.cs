// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Composition;

/// <summary>
///     Marks a layer of a scope chain. An application derives one marker interface per layer (for
///     example editor, session, window and panel) and builds each scope of that layer from modules of
///     that layer only.
/// </summary>
/// <remarks>
///     A module for every layer implements <see cref="IScopeModule{TLayer}" /> of this interface, which
///     makes it usable at any layer.
/// </remarks>
[SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker; it has no members")]
public interface IScopeLayer
{
}
