// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>How the engine reads a cooked content source.</summary>
public enum RuntimeCookedRootKind
{
    /// <summary>A loose cooked root directory.</summary>
    LooseFolder,

    /// <summary>A pak file.</summary>
    PakFile,
}
