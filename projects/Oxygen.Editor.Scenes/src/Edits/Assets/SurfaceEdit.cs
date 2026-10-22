// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;

namespace Oxygen.Editor.Scenes.Edits.Assets;

/// <summary>A surface edit built from an inventory, or why none was built.</summary>
/// <param name="Edit">The edit; <see langword="null"/> when it failed or there is nothing to change.</param>
/// <param name="Error">Why the edit could not be built, if it could not.</param>
public sealed record SurfaceEdit(ISceneEdit? Edit, SceneDiagnostic? Error);
