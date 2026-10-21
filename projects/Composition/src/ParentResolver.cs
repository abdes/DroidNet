// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using DryIoc;

namespace DroidNet.Composition;

/// <summary>
///     The single unknown-service resolver of a scope: a service the scope does not register is resolved
///     by its immediate parent, when the parent chain provides it.
/// </summary>
/// <remarks>
///     <para>
///     The parent builds and caches what it provides, so a parent singleton is the parent's instance
///     whichever scope resolves it first, and a parent transient is built with the parent's
///     dependencies.
///     </para>
///     <para>
///     When the chain does not provide the service, the resolver returns no factory, so optional
///     dependencies and <see cref="IfUnresolved.ReturnDefault" /> get their default instead of an
///     exception. Only positive answers are cached: a registration added to a parent later is seen.
///     </para>
/// </remarks>
/// <param name="parent">The handle of the parent scope.</param>
internal sealed class ParentResolver(ScopeHandle parent)
{
    private readonly ConcurrentDictionary<(Type ServiceType, object? ServiceKey), Factory> factories = new();

    /// <summary>
    ///     Gets the factory that resolves <paramref name="request" /> from the parent.
    /// </summary>
    /// <param name="request">The request the scope cannot satisfy from its own registrations.</param>
    /// <returns>The factory, or <see langword="null" /> when the parent chain does not provide the service.</returns>
    public Factory? Resolve(Request request)
    {
        var serviceType = request.ServiceType;
        var serviceKey = ScopeRegistrations.Normalize(request.ServiceKey);
        return ScopeRegistrations.CanProvide(parent, serviceType, serviceKey)
            ? this.factories.GetOrAdd((serviceType, serviceKey), CreateFactory, parent)
            : null;
    }

    private static DelegateFactory CreateFactory((Type ServiceType, object? ServiceKey) service, ScopeHandle parent)
        => new(_ => parent.Container.Resolve(service.ServiceType, service.ServiceKey, IfUnresolved.Throw));
}
