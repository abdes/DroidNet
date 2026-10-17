// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>A kind and what a component of that kind starts as.</summary>
/// <param name="Kind">The kind <see cref="SceneComponents.Add"/> takes.</param>
/// <param name="Verify">Asserts the defaults of the component that was added.</param>
public sealed record DefaultsCase(string Kind, Action<GameComponent> Verify);
