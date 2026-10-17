// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Tests.Edits.Settings;

/// <summary>A set of scene references one of whose entries breaks a reference rule.</summary>
/// <param name="References">The references the edit is asked to set.</param>
public sealed record ReferencesCase(SceneReferencesData References);
