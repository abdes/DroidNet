// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Reports an ordered change to the native content facts.</summary>
/// <param name="snapshot">The content state after the transition.</param>
public sealed class RuntimeContentChangedEventArgs(RuntimeContentSnapshot snapshot) : EventArgs
{
    /// <summary>Gets the immutable content state after the transition.</summary>
    public RuntimeContentSnapshot Snapshot { get; } = snapshot;
}
