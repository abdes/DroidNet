// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// An input source: its current <see cref="InputSource"/>, replaced whole on each change, and the actions and mapping
/// contexts it declares, each identified under the source.
/// </summary>
/// <remarks>
/// The source's identity is its file's path without the authoring <c>.json</c> (<c>/Content/Input/Player.input</c>);
/// each declared action and context is a product named under it (<c>/Content/Input/Player.input/Jump.oiact</c>). A
/// mapping names an action of its own source by name and an action of another source by virtual path.
/// </remarks>
public sealed class InputContainer
{
    /// <summary>Initializes a new instance of the <see cref="InputContainer"/> class.</summary>
    /// <param name="identity">The source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <param name="source">The source as read.</param>
    public InputContainer(AssetPath identity, InputSource source)
    {
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(source);
        this.Identity = identity;
        this.Source = source;
        this.Actions = this.ActionsOf(source);
        this.Contexts = this.ContextsOf(source);
    }

    /// <summary>Occurs on the thread that replaced <see cref="Source"/>, after it changed.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the source's identity.</summary>
    public AssetPath Identity { get; }

    /// <summary>Gets the source's current content.</summary>
    public InputSource Source { get; private set; }

    /// <summary>Gets the actions the source declares, in document order.</summary>
    public IReadOnlyList<InputAction> Actions { get; private set; }

    /// <summary>Gets the mapping contexts the source declares, in document order.</summary>
    public IReadOnlyList<InputMappingContext> Contexts { get; private set; }

    /// <summary>Finds a declared action by its identity.</summary>
    /// <param name="action">The action's identity, such as <c>/Content/Input/Player.input/Jump.oiact</c>.</param>
    /// <returns>The action, or <see langword="null"/> when this source does not declare it.</returns>
    public InputAction? FindAction(AssetPath action)
    {
        ArgumentNullException.ThrowIfNull(action);
        return this.Actions.FirstOrDefault(candidate => candidate.Identity == action);
    }

    /// <summary>Finds a declared mapping context by its identity.</summary>
    /// <param name="context">The context's identity, such as <c>/Content/Input/Player.input/OnFoot.oimap</c>.</param>
    /// <returns>The context, or <see langword="null"/> when this source does not declare it.</returns>
    public InputMappingContext? FindContext(AssetPath context)
    {
        ArgumentNullException.ThrowIfNull(context);
        return this.Contexts.FirstOrDefault(candidate => candidate.Identity == context);
    }

    /// <summary>Finds where an action or a mapping context of this source is declared.</summary>
    /// <param name="product">The product's identity.</param>
    /// <returns>The position of its name, or <see langword="null"/> when this source does not declare it.</returns>
    public SourcePosition? FindDeclaration(AssetPath product)
        => this.FindAction(product)?.Declaration.Position ?? this.FindContext(product)?.Declaration.Position;

    /// <summary>Replaces the source's content.</summary>
    /// <param name="source">The new content.</param>
    public void Replace(InputSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        this.Source = source;
        this.Actions = this.ActionsOf(source);
        this.Contexts = this.ContextsOf(source);
        this.Changed?.Invoke(this, EventArgs.Empty);
    }

    private InputAction[] ActionsOf(InputSource source)
        => [.. source.Actions.Select(action => new InputAction(InputSource.ProductIdentity(this.Identity, action.Name, AssetFileTypes.InputAction), action))];

    private InputMappingContext[] ContextsOf(InputSource source)
        => [.. source.Contexts.Select(context => new InputMappingContext(InputSource.ProductIdentity(this.Identity, context.Name, AssetFileTypes.InputMappingContext), context))];
}
