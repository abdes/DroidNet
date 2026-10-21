// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;

namespace DroidNet.TestHelpers;

/// <summary>
///     Builds a container as an application host builds it, so tests resolve under the same rules.
/// </summary>
/// <remarks>
///     The container is the one <see cref="DryIocServiceProviderFactory" /> gives a host: Microsoft
///     dependency injection rules, disposable-transient tracking and the captive-dependency check on,
///     with the host's logging and options services, including the open generics
///     <c>ILogger&lt;T&gt;</c> and <c>IOptions&lt;T&gt;</c>.
/// </remarks>
public static class HostContainer
{
    /// <summary>
    ///     Creates a container with the rules and the logging and options services of an application host.
    /// </summary>
    /// <param name="configure">Adds further host services, as a host's <c>ConfigureServices</c> does.</param>
    /// <returns>The container. The caller disposes it.</returns>
    public static IContainer Create(Action<IServiceCollection>? configure = null)
    {
        var services = new ServiceCollection().AddLogging().AddOptions();
        configure?.Invoke(services);
        return new Container().WithDependencyInjectionAdapter(services).Container;
    }
}
