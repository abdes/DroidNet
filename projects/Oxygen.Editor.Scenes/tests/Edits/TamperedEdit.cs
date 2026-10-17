// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;

namespace Oxygen.Editor.Scenes.Tests.Edits;

/// <summary>
/// An edit built from inputs a test changes after construction.
/// </summary>
/// <param name="Edit">The edit.</param>
/// <param name="Mutate">Changes the inputs the edit was built from.</param>
public sealed record TamperedEdit(ISceneEdit Edit, Action Mutate);
