// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;

namespace DroidNet.Composition;

/// <summary>
///     What a scope chain provides, as the scope factory and the topology checks of an application see it.
/// </summary>
/// <remarks>
///     A service is identified by its type and its key. A request without a key, or with one of DryIoc's
///     default keys, asks for a default registration, whichever default key it has. A constructed generic type is provided by a
///     registration of its generic type definition (DryIoc's <c>IsRegistered</c> answers no for
///     <c>ILogger&lt;T&gt;</c> when only <c>ILogger&lt;&gt;</c> is registered).
/// </remarks>
public static class ScopeRegistrations
{
    /// <summary>
    ///     Gets whether a scope or one of its ancestors registers a service.
    /// </summary>
    /// <param name="scope">The scope where the chain starts.</param>
    /// <param name="serviceType">The service type.</param>
    /// <param name="serviceKey">The service key, or <see langword="null" /> for the default registration.</param>
    /// <returns><see langword="true" /> when a scope of the chain registers the service.</returns>
    public static bool CanProvide(ScopeHandle scope, Type serviceType, object? serviceKey)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(serviceType);

        for (var current = scope; current is not null; current = current.Parent)
        {
            if (Registers(current.Container, serviceType, serviceKey))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    ///     Finds the registrations of a new scope that shadow a service of its parent chain and that no
    ///     declared shadow covers.
    /// </summary>
    /// <param name="scope">The registrations of the new scope.</param>
    /// <param name="parent">The parent of the new scope.</param>
    /// <param name="shadows">
    ///     The declared shadows. A generic type definition covers its constructed types.
    /// </param>
    /// <returns>The undeclared shadows, in registration order.</returns>
    /// <remarks>
    ///     A registration shadows the parent chain when the chain provides the same type and key (a
    ///     closed generic type is also provided by its definition) or, for a generic type definition,
    ///     when the chain registers one of its constructed types under the same key. Every scope
    ///     shadows its parent's <see cref="ScopeHandle" /> with its own, so that shadow is always
    ///     declared.
    /// </remarks>
    public static IReadOnlyList<ServiceRegistrationInfo> FindUndeclaredShadows(
        IContainer scope,
        ScopeHandle parent,
        IReadOnlyCollection<Type> shadows)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(parent);
        ArgumentNullException.ThrowIfNull(shadows);

        return [.. scope.GetServiceRegistrations().Where(registration => !IsDeclared(registration.ServiceType, shadows) && Shadows(parent, registration))];
    }

    /// <summary>
    ///     Maps a DryIoc default key to <see langword="null" />, so that every request for the default
    ///     registration has the same identity.
    /// </summary>
    /// <param name="serviceKey">The key of a request or a registration.</param>
    /// <returns>The key, or <see langword="null" /> for the default registration.</returns>
    internal static object? Normalize(object? serviceKey) => serviceKey is DefaultKey ? null : serviceKey;

    private static bool Registers(IContainer container, Type serviceType, object? serviceKey)
    {
        var key = Normalize(serviceKey);
        return RegistersKey(container, serviceType, key)
            || (serviceType.IsConstructedGenericType && RegistersKey(container, serviceType.GetGenericTypeDefinition(), key));
    }

    // A default request is served by any default registration: DryIoc keys the default registrations of one type
    // DefaultKey.Of(0), Of(1) and so on, and keeps the index of each one when another is unregistered.
    private static bool RegistersKey(IContainer container, Type serviceType, object? serviceKey)
        => container.GetAllServiceFactories(serviceType).Any(factory => serviceKey is null
            ? factory.Key is null or DefaultKey
            : Equals(factory.Key, serviceKey));

    private static bool IsDeclared(Type serviceType, IReadOnlyCollection<Type> shadows)
        => serviceType == typeof(ScopeHandle)
            || shadows.Contains(serviceType)
            || (serviceType.IsConstructedGenericType && shadows.Contains(serviceType.GetGenericTypeDefinition()));

    private static bool Shadows(ScopeHandle parent, ServiceRegistrationInfo registration)
    {
        var serviceType = registration.ServiceType;
        var serviceKey = Normalize(registration.OptionalServiceKey);
        return CanProvide(parent, serviceType, serviceKey)
            || (serviceType.IsGenericTypeDefinition && RegistersConstructed(parent, serviceType, serviceKey));
    }

    private static bool RegistersConstructed(ScopeHandle parent, Type definition, object? serviceKey)
    {
        for (var current = parent; current is not null; current = current.Parent)
        {
            if (current.Container.GetServiceRegistrations().Any(registration =>
                    registration.ServiceType.IsConstructedGenericType
                    && registration.ServiceType.GetGenericTypeDefinition() == definition
                    && Equals(Normalize(registration.OptionalServiceKey), serviceKey)))
            {
                return true;
            }
        }

        return false;
    }
}
