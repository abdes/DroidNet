// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>An action an input source declares; it cooks to its own asset.</summary>
/// <param name="Identity">The action's identity, under its source: <c>/Content/Input/Player.input/Jump.oiact</c>.</param>
/// <param name="Declaration">The declaration as read.</param>
public sealed record InputAction(AssetPath Identity, InputActionDeclaration Declaration)
{
    /// <summary>Gets the action's name in its source.</summary>
    public string Name => this.Declaration.Name;
}
