// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Stable resource kinds emitted by the native Inspector.</summary>
public enum CookedResourceKind
{
    /// <summary>Not a native value: the report refuses it.</summary>
    None = 0,

    /// <summary>A buffer resource.</summary>
    Buffer = 1,

    /// <summary>A texture resource.</summary>
    Texture = 2,

    /// <summary>A script resource.</summary>
    Script = 3,

    /// <summary>A physics resource.</summary>
    Physics = 4,

    /// <summary>A texture another container holds; the index selects its texture key reference.</summary>
    ExternalTexture = 5,
}
