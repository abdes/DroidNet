// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using DryIoc.Microsoft.DependencyInjection;

namespace DroidNet.Composition;

/// <summary>
///     Builds the scopes of a scope chain. Each scope is a new, empty DryIoc container that registers
///     only what its modules own and resolves everything else from its immediate parent.
/// </summary>
/// <remarks>
///     <para>
///     Every scope starts from the host's Microsoft dependency injection rules
///     (<see cref="DryIocAdapter.MicrosoftDependencyInjectionRules" />), with the
///     captive-dependency check on, disposable-transient tracking off and a single unknown-service
///     resolver that asks the immediate parent. A rules object of a parent is never reused: its resolver
///     would run first and throw for a service that only an intermediate scope registers.
///     </para>
///     <para>
///     Tracking is off because a scope would otherwise dispose the parent instances it returns. A
///     disposable transient that a layer owns opts in with
///     <c>Setup.With(trackDisposableTransient: true)</c>, and its scope disposes it.
///     </para>
///     <para>
///     Collections (<c>IEnumerable&lt;T&gt;</c>, <c>ResolveMany</c>) see only the scope's own
///     registrations, never a parent's.
///     </para>
/// </remarks>
public sealed class ScopeFactory
{
    private readonly Rules rules = DryIocAdapter.MicrosoftDependencyInjectionRules.WithoutTrackingDisposableTransients();

    /// <summary>
    ///     Creates the handle of the root scope over the host's container, and registers it in that
    ///     container.
    /// </summary>
    /// <param name="host">The container the host built. The handle never disposes it.</param>
    /// <returns>The root scope's handle.</returns>
    /// <exception cref="ContainerException">The host already has a root scope.</exception>
    public static ScopeHandle CreateRoot(IContainer host)
    {
        ArgumentNullException.ThrowIfNull(host);
        var handle = new ScopeHandle(host, parent: null, ownsContainer: false);
        host.RegisterInstance(handle, IfAlreadyRegistered.Throw);
        return handle;
    }

    /// <summary>
    ///     Creates the handle of the root scope over the host's container, after registering the root's
    ///     modules into that container, and creates their activation lists.
    /// </summary>
    /// <typeparam name="TLayer">The layer of the root scope.</typeparam>
    /// <param name="host">The container the host built. The handle never disposes it.</param>
    /// <param name="modules">The modules that register what the root scope owns, in registration order.</param>
    /// <returns>The root scope's handle.</returns>
    /// <exception cref="ContainerException">The host already has a root scope.</exception>
    /// <remarks>
    ///     The root has no parent, so its modules declare no shadows. The root registers its handle after
    ///     its modules, then creates the activation list of every module, module by module, in order. The
    ///     host's container belongs to the caller: when activation fails, the caller disposes it.
    /// </remarks>
    public static ScopeHandle CreateRoot<TLayer>(IContainer host, IEnumerable<IScopeModule<TLayer>> modules)
        where TLayer : IScopeLayer
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(modules);

        IReadOnlyList<IScopeModule<TLayer>> rootModules = [.. modules];
        foreach (var module in rootModules)
        {
            module.Register(host);
        }

        var handle = CreateRoot(host);
        Activate(host, rootModules);
        return handle;
    }

    /// <summary>
    ///     Creates a scope of the layer <typeparamref name="TLayer" /> under <paramref name="parent" />.
    /// </summary>
    /// <typeparam name="TLayer">The layer of the new scope.</typeparam>
    /// <param name="parent">The handle of the parent scope, which owns the new scope.</param>
    /// <param name="modules">The modules that register what the new scope owns, in registration order.</param>
    /// <param name="hostInstances">
    ///     Registers the instances the scope's host provides (a window id, a window), after the modules.
    /// </param>
    /// <returns>The handle of the new scope.</returns>
    /// <exception cref="UndeclaredShadowException">
    ///     A registration of the new scope shadows a service of the parent chain that no module declares.
    /// </exception>
    /// <exception cref="ObjectDisposedException"><paramref name="parent" /> has been disposed.</exception>
    /// <remarks>
    ///     The new scope registers its own handle, so a service of the scope that takes a
    ///     <see cref="ScopeHandle" /> receives this scope's, never a parent's. The scope then creates
    ///     the activation list of every module, module by module, in order. When registration or
    ///     activation fails, the new scope's container is disposed and the parent is left unchanged.
    /// </remarks>
    public ScopeHandle Create<TLayer>(
        ScopeHandle parent,
        IEnumerable<IScopeModule<TLayer>> modules,
        Action<IRegistrator>? hostInstances = null)
        where TLayer : IScopeLayer
    {
        ArgumentNullException.ThrowIfNull(parent);
        ArgumentNullException.ThrowIfNull(modules);
        ObjectDisposedException.ThrowIf(parent.IsDisposed, parent);

        IReadOnlyList<IScopeModule<TLayer>> layerModules = [.. modules];
        var resolver = new ParentResolver(parent);
        var container = new Container(this.rules.WithUnknownServiceResolvers(resolver.Resolve));
        var handle = new ScopeHandle(container, parent, ownsContainer: true);
        try
        {
            Register(container, parent, handle, layerModules, hostInstances);
            Activate(container, layerModules);
        }
        catch
        {
            container.Dispose();
            throw;
        }

        if (!parent.TryAttach(handle))
        {
            container.Dispose();
            throw new ObjectDisposedException(nameof(ScopeHandle));
        }

        return handle;
    }

    private static void Register<TLayer>(
        Container container,
        ScopeHandle parent,
        ScopeHandle scope,
        IReadOnlyList<IScopeModule<TLayer>> modules,
        Action<IRegistrator>? hostInstances)
        where TLayer : IScopeLayer
    {
        foreach (var module in modules)
        {
            module.Register(container);
        }

        hostInstances?.Invoke(container);

        container.RegisterInstance(scope);
        var shadows = modules.SelectMany(module => module.Shadows).ToHashSet();
        var undeclared = ScopeRegistrations.FindUndeclaredShadows(container, parent, shadows);
        if (undeclared.Count > 0)
        {
            throw new UndeclaredShadowException(undeclared);
        }
    }

    private static void Activate<TLayer>(IResolver container, IReadOnlyList<IScopeModule<TLayer>> modules)
        where TLayer : IScopeLayer
    {
        foreach (var serviceType in modules.SelectMany(module => module.Activation))
        {
            _ = container.Resolve(serviceType);
        }
    }
}
