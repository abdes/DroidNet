// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A correlated input event for one live view generation.</summary>
/// <param name="OperationId">The request identity.</param>
/// <param name="Target">The view generation captured by the UI.</param>
/// <param name="Input">The input intent.</param>
public sealed record RuntimeInputRequest(Guid OperationId, RuntimeViewTarget Target, RuntimeInputEvent Input);
