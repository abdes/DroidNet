// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>An immutable, correlated scene mutation request.</summary>
/// <param name="OperationId">The request identity.</param>
/// <param name="Target">The scene activation that must still be current at dispatch.</param>
/// <param name="Command">The node identity and projection values.</param>
public sealed record RuntimeWorldRequest(Guid OperationId, RuntimeSceneTarget Target, RuntimeWorldCommand Command);
