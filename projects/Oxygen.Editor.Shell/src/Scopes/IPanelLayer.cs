// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Composition;

namespace Oxygen.Editor.Shell.Scopes;

/// <summary>
///     The layer of a panel's scope: the panel's own router, view location and the services only the panel uses.
/// </summary>
[SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker; it has no members")]
public interface IPanelLayer : IScopeLayer
{
}
