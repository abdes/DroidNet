// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Composition;

/// <summary>
///     The layer of the application's scope, the root of the chain: one per process, over the host's container. It
///     holds the services that every session and window of the application shares.
/// </summary>
[SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker; it has no members")]
public interface IApplicationLayer : IScopeLayer
{
}
