// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Composition;

/// <summary>
///     The layer of a session's scope: one unit of work the application opens and closes as a whole, whose services
///     every window of that session shares.
/// </summary>
[SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker; it has no members")]
public interface ISessionLayer : IScopeLayer
{
}
