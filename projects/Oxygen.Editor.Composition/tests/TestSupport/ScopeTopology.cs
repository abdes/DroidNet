// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.Composition.UI.Tests.TestSupport;

/// <summary>
///     Reads a scope's registrations and the constructors of the types it builds, for the topology rules of the editor's
///     scope chain.
/// </summary>
/// <remarks>
///     Only reflection registrations have constructors to read. A delegate registration's body is opaque: the topology
///     tests list each one, reviewed. Instances, such as a scope's handle or a window id, have no dependencies.
/// </remarks>
internal static class ScopeTopology
{
    // The types through which a service would locate others instead of receiving them.
    private static readonly Type[] Locators = [typeof(IContainer), typeof(IResolver), typeof(IResolverContext), typeof(IRegistrator), typeof(IServiceProvider)];

    // The collection types a constructor receives: DryIoc fills them with the scope's own registrations only.
    private static readonly Type[] Collections = [typeof(IEnumerable<>), typeof(IReadOnlyList<>), typeof(IReadOnlyCollection<>), typeof(IList<>), typeof(ICollection<>)];

    // The wrappers DryIoc resolves around a service: the service they wrap must be provided.
    private static readonly Type[] Wrappers = [typeof(Lazy<>), typeof(Func<>), typeof(Func<,>), typeof(Func<,,>), typeof(Func<,,,>)];

    /// <summary>Gets the registrations a scope owns.</summary>
    /// <param name="scope">The scope.</param>
    /// <returns>The scope's own registrations.</returns>
    public static IReadOnlyList<ServiceRegistrationInfo> Owned(ScopeHandle scope)
        => [.. scope.Resolver.Resolve<IContainer>().GetServiceRegistrations()];

    /// <summary>Gets the name of a type as C# writes it, with the names of its type arguments.</summary>
    /// <param name="type">The type.</param>
    /// <returns>The name, for example <c>ISettingsService&lt;IAppearanceSettings&gt;</c>.</returns>
    public static string NameOf(Type type)
        => type.IsConstructedGenericType
            ? $"{type.Name[..type.Name.IndexOf('`', StringComparison.Ordinal)]}<{string.Join(", ", type.GenericTypeArguments.Select(NameOf))}>"
            : type.Name;

    /// <summary>Gets the delegate registrations of a scope.</summary>
    /// <param name="registrations">The scope's registrations.</param>
    /// <returns>
    ///     The name of the service type (see <see cref="NameOf" />) and the key, or <see langword="null" /> for the
    ///     default registration, of each registration whose factory is a delegate.
    /// </returns>
    public static IReadOnlySet<(string Service, object? Key)> Delegates(IEnumerable<ServiceRegistrationInfo> registrations)
        => registrations
            .Where(static registration => registration.Factory is DelegateFactory)
            .Select(static registration => (NameOf(registration.ServiceType), registration.OptionalServiceKey is DefaultKey ? null : registration.OptionalServiceKey))
            .ToHashSet();

    /// <summary>Gets the types a scope builds by reflection, each with the constructor DryIoc calls.</summary>
    /// <param name="registrations">The scope's registrations.</param>
    /// <returns>The implementation types and their constructors, each once.</returns>
    public static IReadOnlyList<(Type Type, ConstructorInfo Constructor)> Built(IEnumerable<ServiceRegistrationInfo> registrations)
    {
        List<(Type Type, ConstructorInfo Constructor)> built = [];
        foreach (var type in registrations.Select(static registration => registration.Factory).OfType<ReflectionFactory>().Select(static factory => factory.ImplementationType).Distinct())
        {
            if (type is { IsGenericTypeDefinition: false } && ConstructorOf(type) is { } constructor)
            {
                built.Add((type, constructor));
            }
        }

        return built;
    }

    /// <summary>Gets the types a scope builds as singletons that are views.</summary>
    /// <param name="registrations">The scope's registrations.</param>
    /// <returns>The view types registered as singletons.</returns>
    public static IReadOnlySet<Type> SingletonViews(IEnumerable<ServiceRegistrationInfo> registrations)
        => registrations
            .Where(static registration => registration.Factory is ReflectionFactory { Reuse: SingletonReuse })
            .Select(static registration => registration.Factory.ImplementationType!)
            .Where(static type => typeof(UIElement).IsAssignableFrom(type))
            .ToHashSet();

    /// <summary>Gets the constructor parameters through which a type would locate services.</summary>
    /// <param name="constructor">The constructor.</param>
    /// <returns>The parameters of a container, resolver or service provider type.</returns>
    public static IEnumerable<ParameterInfo> LocatorParameters(ConstructorInfo constructor)
        => constructor.GetParameters().Where(static parameter => Locators.Contains(parameter.ParameterType));

    /// <summary>Gets the constructor parameters of a collection of a service that only a parent scope registers.</summary>
    /// <param name="scope">The scope that builds the type.</param>
    /// <param name="constructor">The constructor.</param>
    /// <returns>The parameters whose collection the scope would leave empty.</returns>
    public static IEnumerable<ParameterInfo> CrossLayerCollections(ScopeHandle scope, ConstructorInfo constructor)
    {
        var owned = Owned(scope).Select(static registration => registration.ServiceType).ToHashSet();
        return constructor.GetParameters().Where(parameter =>
            ElementOf(parameter.ParameterType) is { } element
            && !owned.Contains(element)
            && scope.Parent is { } parent
            && ScopeRegistrations.CanProvide(parent, element, serviceKey: null));
    }

    /// <summary>Gets the required constructor parameters that the scope chain does not provide.</summary>
    /// <param name="scope">The scope that builds the type.</param>
    /// <param name="constructor">The constructor.</param>
    /// <returns>The parameters DryIoc could not resolve.</returns>
    public static IEnumerable<ParameterInfo> Unresolvable(ScopeHandle scope, ConstructorInfo constructor)
        => constructor.GetParameters().Where(parameter =>
            !parameter.HasDefaultValue
            && !Locators.Contains(parameter.ParameterType)
            && ElementOf(parameter.ParameterType) is null
            && !ScopeRegistrations.CanProvide(scope, Unwrap(parameter.ParameterType), parameter.GetCustomAttribute<FromKeyedServicesAttribute>()?.Key));

    // The constructor DryIoc calls under the Microsoft rules: the public one with the most parameters.
    private static ConstructorInfo? ConstructorOf(Type type)
        => type.GetConstructors().OrderByDescending(static constructor => constructor.GetParameters().Length).FirstOrDefault();

    private static Type? ElementOf(Type type)
        => type.IsArray ? type.GetElementType()
            : type.IsConstructedGenericType && Collections.Contains(type.GetGenericTypeDefinition()) ? type.GenericTypeArguments[0]
            : null;

    private static Type Unwrap(Type type)
        => type.IsConstructedGenericType && Wrappers.Contains(type.GetGenericTypeDefinition())
            ? type.GenericTypeArguments[^1]
            : type;
}
