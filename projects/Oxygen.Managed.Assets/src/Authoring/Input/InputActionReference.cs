// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>
/// An action a mapping names: the name of an action its source declares (<c>Jump</c>), or the virtual path of an
/// action (<c>/Content/Input/Common.input/Jump.oiact</c>).
/// </summary>
/// <param name="Text">The reference as authored.</param>
public sealed record InputActionReference(string Text)
{
    /// <summary>Gets where the reference is.</summary>
    public SourcePosition? Position { get; init; }

    /// <summary>Gets a value indicating whether the reference is a virtual path rather than a local name.</summary>
    public bool IsPath => this.Text.StartsWith('/');

    /// <summary>Gets the identity of the action the reference names.</summary>
    /// <param name="source">The identity of the source the reference is in.</param>
    /// <param name="action">The action's identity.</param>
    /// <returns><see langword="false"/> when the reference is a path that is not a virtual path below a mount.</returns>
    public bool TryGetAction(AssetPath source, [NotNullWhen(true)] out AssetPath? action)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (!this.IsPath)
        {
            action = InputSource.ProductIdentity(source, this.Text, AssetFileTypes.InputAction);
            return true;
        }

        action = AssetPath.TryParse(this.Text, out var path) && !path.IsMount ? path : null;
        return action is not null;
    }
}
