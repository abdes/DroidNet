// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>The kind of value a model import setting holds in the import manifest's vocabulary.</summary>
public enum ModelImportSettingKind
{
    /// <summary>One string of a fixed set, such as <c>unit_policy</c>.</summary>
    Choice,

    /// <summary>A real number, such as <c>unit_scale</c>.</summary>
    Number,

    /// <summary>A whole number, such as <c>max_size</c>.</summary>
    Integer,

    /// <summary>A boolean, such as <c>import_lights</c>.</summary>
    Toggle,

    /// <summary>A string naming something in the file, such as <c>gltf_scene</c>.</summary>
    Text,
}
