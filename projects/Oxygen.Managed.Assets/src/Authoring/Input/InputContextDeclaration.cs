// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>A mapping context an input source declares.</summary>
/// <param name="Name">The context's name; its identity is the source's identity followed by <c>/Name.oimap</c>.</param>
public sealed record InputContextDeclaration(string Name)
{
    /// <summary>Gets the priority the context is activated with when none is given.</summary>
    public int Priority { get; init; }

    /// <summary>Gets where the context's name is declared.</summary>
    public SourcePosition? Position { get; init; }

    /// <summary>
    /// Gets every action the context's mappings name, in document order: each mapping's <c>action</c>, then its
    /// triggers' <c>chord_action</c>, <c>combo_actions</c> and <c>aux</c> actions.
    /// </summary>
    public IReadOnlyList<InputActionReference> ActionReferences { get; init; } = [];
}
