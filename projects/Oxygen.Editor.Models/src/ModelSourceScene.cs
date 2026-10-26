// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>One scene of a glTF source, as its analysis reports it.</summary>
/// <param name="Index">The scene's index in the file.</param>
/// <param name="Name">The authored name; empty when the scene has none.</param>
/// <param name="Selector">What the <c>gltf_scene</c> setting names the scene by: its name, or <c>#N</c>.</param>
/// <param name="IsDefault">Whether an import without a chosen scene takes this one.</param>
/// <param name="RootCount">The scene's root nodes.</param>
/// <param name="NodeCount">The distinct nodes reachable from its roots.</param>
public sealed record ModelSourceScene(int Index, string Name, string Selector, bool IsDefault, int RootCount, int NodeCount);
