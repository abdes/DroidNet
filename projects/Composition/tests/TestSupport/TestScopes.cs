// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using DryIoc.Microsoft.DependencyInjection;

namespace DroidNet.Composition.Tests.TestSupport;

/// <summary>Builds the test chain root &gt; session &gt; window &gt; panel.</summary>
internal static class TestScopes
{
    /// <summary>
    ///     Creates a host container with the rules the application host uses: Microsoft rules, with
    ///     disposable-transient tracking and the captive-dependency check on.
    /// </summary>
    /// <param name="register">Registers the root's services.</param>
    /// <returns>The host container.</returns>
    public static Container CreateHost(Action<IRegistrator>? register = null)
    {
        var host = new Container(DryIocAdapter.MicrosoftDependencyInjectionRules);
        register?.Invoke(host);
        return host;
    }

    /// <summary>Creates a scope from one test module.</summary>
    /// <typeparam name="TLayer">The scope's layer.</typeparam>
    /// <param name="factory">The scope factory.</param>
    /// <param name="parent">The parent scope.</param>
    /// <param name="register">Registers the scope's services.</param>
    /// <returns>The new scope.</returns>
    public static ScopeHandle CreateScope<TLayer>(this ScopeFactory factory, ScopeHandle parent, Action<IRegistrator>? register = null)
        where TLayer : IScopeLayer
        => factory.Create<TLayer>(parent, [new TestModule<TLayer>(register ?? (_ => { }))]);
}
