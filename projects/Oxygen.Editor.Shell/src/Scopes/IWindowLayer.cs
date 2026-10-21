// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Composition;

namespace Oxygen.Editor.Shell.Scopes;

/// <summary>
///     The layer of a window's scope: the window's router and view location, its docking, its documents and the
///     services of the features it shows.
/// </summary>
[SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker; it has no members")]
public interface IWindowLayer : IScopeLayer
{
}
