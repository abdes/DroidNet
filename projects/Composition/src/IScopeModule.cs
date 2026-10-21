// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;

namespace DroidNet.Composition;

/// <summary>
///     Registers what one subsystem owns in a scope of the layer <typeparamref name="TLayer" />.
/// </summary>
/// <typeparam name="TLayer">
///     The layer the module registers into. The parameter is contravariant: a module of
///     <see cref="IScopeLayer" /> is a module of every layer.
/// </typeparam>
/// <remarks>
///     A scope is an empty container over its parent, so a module registers only what its layer owns;
///     anything else is resolved by the parent, built with the parent's dependencies and never disposed
///     by the scope.
/// </remarks>
public interface IScopeModule<in TLayer>
    where TLayer : IScopeLayer
{
    /// <summary>
    ///     Gets the service types that this module registers although a parent scope also registers them.
    ///     A generic type definition covers its constructed types. <see cref="ScopeFactory" /> refuses a
    ///     scope with a shadow that no module of the scope declares.
    /// </summary>
    public IReadOnlyCollection<Type> Shadows { get; }

    /// <summary>
    ///     Gets the services the scope creates when it opens, in this order, after every module of the
    ///     scope has registered.
    /// </summary>
    public IReadOnlyList<Type> Activation { get; }

    /// <summary>
    ///     Registers the module's services into a new scope.
    /// </summary>
    /// <param name="scope">The registrations of the scope being created.</param>
    public void Register(IRegistrator scope);
}
