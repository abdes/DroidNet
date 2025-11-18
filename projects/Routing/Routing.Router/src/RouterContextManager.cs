// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.Events;

namespace DroidNet.Routing;

/// <summary>
/// Manages the routing contexts within the application, providing methods to retrieve and manage navigation contexts for different targets.
/// </summary>
/// <remarks>
/// The <see cref="RouterContextManager"/> class is responsible for managing the lifecycle and
/// retrieval of navigation contexts. It ensures that each target has an appropriate context and
/// facilitates the transition between different navigation states.
/// <para>
/// At a minimum, the router has one main context, corresponding to the special <see cref="Target.Main" />
/// target, and may use multiple other contexts during the application's lifetime. Such additional
/// contexts are obtained from a <see cref="IContextProvider" />, and only one at a time is used, the
/// current context.
/// </para>
/// </remarks>
public sealed class RouterContextManager : IDisposable
{
    private readonly Lazy<NavigationContext> lazyMainContext;
    private readonly IContextProvider<NavigationContext> contextProvider;
    private readonly EventHandler<ContextEventArgs> onContextChanged;

    private bool isDisposed;

    private NavigationContext? currentContext;

    /// <summary>
    /// Initializes a new instance of the <see cref="RouterContextManager"/> class.
    /// </summary>
    /// <param name="contextProvider">The context provider used to create and manage navigation contexts.</param>
    public RouterContextManager(IContextProvider<NavigationContext> contextProvider)
    {
        this.contextProvider = contextProvider;

        // Register for context change notification and update the current
        // context accordingly.
        this.onContextChanged = (_, args) =>
        {
            if (args.Context == this.currentContext)
            {
                return;
            }

            this.currentContext = args.Context as NavigationContext;
        };
        this.contextProvider.ContextChanged += this.onContextChanged;

        this.lazyMainContext = new Lazy<NavigationContext>(() => contextProvider.ContextForTarget(Target.Main));
    }

    /// <summary>
    /// Gets the <see cref="NavigationContext" /> instance for the special target <see cref="Target.Main" />.
    /// </summary>
    /// <remarks>
    /// For a certain <see cref="Router" />, there is only one routing context for the special
    /// target <see cref="Target.Main" />, and within an application, there is usually a single
    /// top-level router.
    /// </remarks>
    private NavigationContext MainContext => this.lazyMainContext.Value;

    /// <summary>
    /// Gets the router context for the specified target.
    /// </summary>
    /// <param name="target">
    /// The target for which to get the router context. This can be <see langword="null"/>, <see cref="Target.Self"/>,
    /// <see cref="Target.Main"/>, or any other application-specific target name.
    /// </param>
    /// <param name="options">The navigation options for this navigation, if any. Used to extract target-replacement instructions.</param>
    /// <returns>
    /// The <see cref="NavigationContext"/> for the specified target.
    /// </returns>
    /// <remarks>
    /// If the target is <see langword="null"/> or <see cref="Target.Self"/>, the method returns the
    /// current active context, or the special main context if there is no active context yet.
    /// If the target is <see cref="Target.Main"/>, the method returns the main context. For any
    /// other target, the method returns a new context for the specified target, created by the
    /// route activator.
    /// </remarks>
    public NavigationContext GetContextForTarget(Target? target, NavigationOptions? options = null)
    {
        if (target?.IsSelf != false)
        {
            // If target is not specified or is Target.Self, then we should
            // use the current active context, unless there is no active
            // context yet. In such case, we fall back to the main context.
            return this.currentContext ?? this.MainContext;
        }

        return target.IsMain ? this.MainContext : this.contextProvider.ContextForTarget(target, this.currentContext, options);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.contextProvider.ContextChanged -= this.onContextChanged;
        this.isDisposed = true;
    }
}
