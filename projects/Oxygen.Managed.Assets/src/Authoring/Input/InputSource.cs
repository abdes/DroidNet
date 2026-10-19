// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>
/// An input source, read from <c>*.input.json</c>: a container of input actions and mapping contexts.
/// </summary>
/// <remarks>
/// The source's identity is its path without the authoring <c>.json</c> (<c>/Content/Input/Player.input</c>). Each
/// declared action and context is a product named under it (<c>/Content/Input/Player.input/Jump.oiact</c>), so two
/// sources never produce the same identity and moving a source moves its products. A mapping names an action of this
/// source by its name and an action of another source by its virtual path.
/// </remarks>
public sealed record InputSource
{
    /// <summary>Gets the actions the source declares, in document order.</summary>
    public IReadOnlyList<InputActionDeclaration> Actions { get; init; } = [];

    /// <summary>Gets the mapping contexts the source declares, in document order.</summary>
    public IReadOnlyList<InputContextDeclaration> Contexts { get; init; } = [];

    /// <summary>Gets every action reference of every context, in document order.</summary>
    public IEnumerable<InputActionReference> ActionReferences => this.Contexts.SelectMany(static context => context.ActionReferences);

    /// <summary>Gets the identity of a product of an input source.</summary>
    /// <param name="source">The source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <param name="name">The product's name, such as <c>Jump</c>.</param>
    /// <param name="extension">The product's cooked extension: <see cref="AssetFileTypes.InputAction"/> or
    /// <see cref="AssetFileTypes.InputMappingContext"/>.</param>
    /// <returns>The product's identity, such as <c>/Content/Input/Player.input/Jump.oiact</c>.</returns>
    public static AssetPath ProductIdentity(AssetPath source, string name, string extension)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentException.ThrowIfNullOrEmpty(name);
        ArgumentException.ThrowIfNullOrEmpty(extension);
        return source.Combine(name + extension);
    }

    /// <summary>Finds a declared action by name.</summary>
    /// <param name="name">The action's name.</param>
    /// <returns>The declaration, or <see langword="null"/> when the source does not declare it.</returns>
    public InputActionDeclaration? FindAction(string name)
        => this.Actions.FirstOrDefault(action => string.Equals(action.Name, name, StringComparison.Ordinal));

    /// <summary>Finds a declared mapping context by name.</summary>
    /// <param name="name">The context's name.</param>
    /// <returns>The declaration, or <see langword="null"/> when the source does not declare it.</returns>
    public InputContextDeclaration? FindContext(string name)
        => this.Contexts.FirstOrDefault(context => string.Equals(context.Name, name, StringComparison.Ordinal));

    /// <summary>Finds the declaration of a product, from the product's cooked extension.</summary>
    /// <param name="name">The product's name.</param>
    /// <param name="extension">The product's cooked extension.</param>
    /// <returns>The position of the declaration's name, or <see langword="null"/> when the source does not declare it.</returns>
    public SourcePosition? FindDeclaration(string name, string extension)
        => extension switch
        {
            AssetFileTypes.InputAction => this.FindAction(name)?.Position,
            AssetFileTypes.InputMappingContext => this.FindContext(name)?.Position,
            _ => null,
        };
}
