// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Composition;

/// <summary>
///     The layer of a panel's scope, inside a window: the services only that panel uses, such as its own router and
///     view location.
/// </summary>
[SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker; it has no members")]
public interface IPanelLayer : IScopeLayer
{
}
