// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>
/// An action a mapping names: the name of an action its source declares (<c>Jump</c>), or a reference to an action,
/// which ends with <c>.oiact</c> (<c>Car.input/Jump.oiact</c>, <c>/Props/Input/Shared.input/Use.oiact</c>). The schema
/// refuses an action name that ends so, which keeps the two apart.
/// </summary>
/// <param name="Text">The reference as authored.</param>
public sealed record InputActionReference(string Text)
{
    /// <summary>Gets where the reference is.</summary>
    public SourcePosition? Position { get; init; }

    /// <summary>Gets a value indicating whether the text is a reference rather than a local name.</summary>
    public bool IsReference => this.Text.EndsWith(AssetFileTypes.InputAction, StringComparison.Ordinal);

    /// <summary>Gets the identity of the action the reference names.</summary>
    /// <param name="source">The identity of the source the reference is in.</param>
    /// <param name="action">The action's identity.</param>
    /// <returns><see langword="false"/> when the text is a reference that does not resolve.</returns>
    public bool TryGetAction(AssetPath source, [NotNullWhen(true)] out AssetPath? action)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (!this.IsReference)
        {
            action = InputSource.ProductIdentity(source, this.Text, AssetFileTypes.InputAction);
            return true;
        }

        return AssetPath.TryResolveReference(source, this.Text, VirtualPathRuleSet.Authored, out action, out _);
    }
}
