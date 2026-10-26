// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>The source formats a model import setting applies to.</summary>
[Flags]
public enum ModelSourceFormats
{
    /// <summary>No format.</summary>
    None = 0,

    /// <summary>A glTF file, JSON or binary (<c>.gltf</c>, <c>.glb</c>).</summary>
    Gltf = 1,

    /// <summary>An FBX file.</summary>
    Fbx = 2,

    /// <summary>Every model format.</summary>
    All = Gltf | Fbx,
}
