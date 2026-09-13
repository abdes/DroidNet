// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A current asset intent and its native application outcome.</summary>
/// <param name="Request">The authored operation and target.</param>
/// <param name="Generation">The last observed native completion generation, or zero while initially pending.</param>
/// <param name="Succeeded">Null while pending, true after application, false after failure.</param>
/// <param name="Reason">The current load or application failure.</param>
public sealed record RuntimeAssetRequestStatus(RuntimeWorldRequest Request, ulong Generation, bool? Succeeded, string? Reason = null);
