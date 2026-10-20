// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>Says which input source changed.</summary>
/// <param name="container">The identity of the source that was added, changed or removed; <see langword="null"/> when
/// the project changed and every source may have.</param>
public sealed class InputSourceChangedEventArgs(AssetPath? container) : EventArgs
{
    /// <summary>Gets the identity of the source that changed, or <see langword="null"/> when every source may have.</summary>
    public AssetPath? Container { get; } = container;
}
