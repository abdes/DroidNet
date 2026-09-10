// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Selects which side of a <see cref="PropertyOp"/> to apply.
/// </summary>
public enum ApplySide
{
    /// <summary>Apply the pre-edit snapshot (used by undo).</summary>
    Before,

    /// <summary>Apply the post-edit snapshot (used by do/redo).</summary>
    After,
}
