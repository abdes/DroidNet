// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Composition;

/// <summary>
///     The layer of a window's scope: the window's own services, such as its router, its view location and what its
///     content shows.
/// </summary>
[SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker; it has no members")]
public interface IWindowLayer : IScopeLayer
{
}
