// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.SceneInspector;

/// <summary>One line of a model's facts in the Properties pane, such as "Meshes 12".</summary>
/// <param name="Label">What the line counts or names, such as "Meshes".</param>
/// <param name="Value">Its value, such as "12".</param>
public sealed record ModelFactRow(string Label, string Value);
