// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;

namespace DroidNet.Composition.Tests.TestSupport;

/// <summary>A module of the layer <typeparamref name="TLayer" /> whose registrations a test writes.</summary>
/// <typeparam name="TLayer">The module's layer.</typeparam>
/// <param name="register">Registers the module's services.</param>
internal sealed class TestModule<TLayer>(Action<IRegistrator> register) : IScopeModule<TLayer>
    where TLayer : IScopeLayer
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows { get; init; } = [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation { get; init; } = [];

    /// <inheritdoc />
    public void Register(IRegistrator scope) => register(scope);
}
