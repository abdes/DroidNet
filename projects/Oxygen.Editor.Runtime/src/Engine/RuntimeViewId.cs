// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>An engine view identity that can be used before Interop is loaded.</summary>
/// <param name="Value">The runtime view identifier.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimeViewId(ulong Value)
{
    /// <summary>Gets a value indicating whether this identifies a created runtime view.</summary>
    public bool IsValid => this.Value is not (0 or ulong.MaxValue);

    /// <summary>Gets the invalid view sentinel.</summary>
    public static RuntimeViewId Invalid => new(ulong.MaxValue);
}
