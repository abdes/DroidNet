// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A current native asset-load failure, distinct from dispatch acceptance.</summary>
/// <param name="request">The original managed operation and target.</param>
/// <param name="generation">The authoritative native asset-request generation.</param>
/// <param name="message">The native load or application failure.</param>
public sealed class RuntimeAssetLoadFailedEventArgs(RuntimeWorldRequest request, ulong generation, string message) : EventArgs
{
    /// <summary>Gets the original request, including scene, node, asset and slot identity.</summary>
    public RuntimeWorldRequest Request { get; } = request;

    /// <summary>Gets the request generation assigned by the native loader authority.</summary>
    public ulong Generation { get; } = generation;

    /// <summary>Gets the native failure detail.</summary>
    public string Message { get; } = message;
}
