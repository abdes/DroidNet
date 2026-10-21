// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;

namespace Oxygen.Editor.Routing;

/// <summary>
///     Gives a scope its own local router and view location: the routes, the router services, the
///     local router context, <see cref="IViewLocator" /> and <see cref="ViewModelToView" />. Every window
///     and panel scope includes it.
/// </summary>
/// <remarks>
///     The router, its services and the view location shadow the root's, and the local router context
///     shadows the one of an enclosing routed scope, so the module declares them.
///     When the scope opens, the context's <see cref="LocalRouterContext.LocalRouter" /> becomes the
///     scope's router.
/// </remarks>
/// <param name="routes">The scope's routes.</param>
/// <param name="context">The scope's local router context.</param>
public sealed class RouterScopeModule(IRoutes routes, LocalRouterContext context) : IScopeModule<IScopeLayer>
{
    private static readonly Type[] RouterServices =
    [
        typeof(IRoutes),
        typeof(IRouterStateManager),
        typeof(IRouteActivator),
        typeof(IUrlParser),
        typeof(IUrlSerializer),
        typeof(IContextProvider<NavigationContext>),
        typeof(IContextProvider),
        typeof(RouterContextManager),
        typeof(IRouter),
        typeof(IViewLocator),
        typeof(ViewModelToView),

        // A panel's routed scope nests under its window's routed scope.
        typeof(LocalRouterContext),
        typeof(ILocalRouterContext),
        typeof(INavigationContext),
        typeof(LocalRouterBinding),
    ];

    private static readonly Type[] Activated = [typeof(LocalRouterBinding)];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => RouterServices;

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        scope.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        scope.Register<ViewModelToView>(Reuse.Singleton);

        scope.RegisterInstance(routes);
        scope.RegisterInstance(context);
        scope.RegisterInstance<ILocalRouterContext>(context);
        scope.RegisterInstance<INavigationContext>(context);
        scope.Register<IRouterStateManager, RouterStateManager>(Reuse.Singleton);
        scope.Register<IRouteActivator, LocalRouteActivator>(Reuse.Singleton);
        scope.Register<IUrlParser, DefaultUrlParser>(Reuse.Singleton);
        scope.Register<IUrlSerializer, DefaultUrlSerializer>(Reuse.Singleton);
        scope.Register<IContextProvider<NavigationContext>, LocalRouterContextProvider>(Reuse.Singleton);
        scope.RegisterDelegate<IContextProvider>(r => r.Resolve<IContextProvider<NavigationContext>>());
        scope.Register<RouterContextManager>(Reuse.Singleton);
        scope.Register<IRouter, Router>(Reuse.Singleton);
        scope.Register<LocalRouterBinding>(Reuse.Singleton);
    }
}
