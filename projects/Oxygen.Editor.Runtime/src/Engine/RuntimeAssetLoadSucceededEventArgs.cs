// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Reports application of a current native asset request.</summary>
/// <param name="request">The original managed intent.</param>
/// <param name="generation">The native generation that was applied.</param>
public sealed class RuntimeAssetLoadSucceededEventArgs(RuntimeWorldRequest request, ulong generation) : EventArgs
{
    /// <summary>Gets the original operation and target.</summary>
    public RuntimeWorldRequest Request { get; } = request;

    /// <summary>Gets the native generation that was applied.</summary>
    public ulong Generation { get; } = generation;
}
