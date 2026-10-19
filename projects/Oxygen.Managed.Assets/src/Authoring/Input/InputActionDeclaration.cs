// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>An action an input source declares.</summary>
/// <param name="Name">The action's name; its identity is the source's identity followed by <c>/Name.oiact</c>.</param>
/// <param name="ValueType">The action's value type: <c>bool</c>, <c>axis1d</c> or <c>axis2d</c>.</param>
public sealed record InputActionDeclaration(string Name, string ValueType)
{
    /// <summary>Gets a value indicating whether the action consumes the input that triggers it.</summary>
    public bool ConsumesInput { get; init; }

    /// <summary>Gets how the values of several bindings combine: <c>highest_absolute</c> or <c>cumulative</c>.</summary>
    public string Accumulation { get; init; } = "highest_absolute";

    /// <summary>Gets where the action's name is declared.</summary>
    public SourcePosition? Position { get; init; }
}
