// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <summary>The kinds of file New Script, New Input File and New Scene create.</summary>
public enum AuthoredAssetKind
{
    /// <summary>A Lua script with one example parameter and the two most common hooks.</summary>
    Script,

    /// <summary>An input file with one action and one mapping context named after the file.</summary>
    InputFile,

    /// <summary>An empty scene with an identity of its own, named after its file.</summary>
    Scene,
}
