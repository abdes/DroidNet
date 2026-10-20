// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>A mapping context an input source declares; it cooks to its own asset.</summary>
/// <param name="Identity">The context's identity, under its source: <c>/Content/Input/Player.input/OnFoot.oimap</c>.</param>
/// <param name="Declaration">The declaration as read.</param>
public sealed record InputMappingContext(AssetPath Identity, InputContextDeclaration Declaration)
{
    /// <summary>Gets the context's name in its source.</summary>
    public string Name => this.Declaration.Name;

    /// <summary>Gets the priority the context is activated with when none is given.</summary>
    public int Priority => this.Declaration.Priority;
}
