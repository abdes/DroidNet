// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed RuntimeMouseButton values; native mapping belongs to the Runtime adapter.</summary>
public enum RuntimeMouseButton
{
    /// <summary>The None value.</summary>
    None = 0,

    /// <summary>The Left value.</summary>
    Left = 1,

    /// <summary>The Right value.</summary>
    Right = 2,

    /// <summary>The Middle value.</summary>
    Middle = 3,

    /// <summary>The ExtButton1 value.</summary>
    ExtButton1 = 4,

    /// <summary>The ExtButton2 value.</summary>
    ExtButton2 = 5,
}
