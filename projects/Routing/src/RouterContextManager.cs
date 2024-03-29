// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Manages <see cref="RouterContext" /> instances used by the router.
/// </summary>
/// <remarks>
/// At a minimum, the router has one main context, corresponding to the special
/// <see cref="Target.Main" /> target, and may use multiple other contexts
/// during the application's lifetime. Such additional contexts are obtained
/// from a <see cref="IContextProvider" />, and only one at a time is used, the
/// current context.
/// <para>
/// This context manager only manages the main and current contexts.
/// </para>
/// </remarks>
public class RouterContextManager : IDisposable
{
    private readonly Lazy<RouterContext> lazyMainContext;
    private readonly IContextProvider contextProvider;
    private readonly EventHandler<ContextEventArgs> onContextChanged;

    /// <summary>
    /// Gets a value representing the current routing context.
    /// </summary>
    /// <value>
    /// An instance of <see cref="RouterContext" /> representing the current
    /// routing context.
    /// </value>
    private RouterContext? currentContext;

    /// <summary>
    /// Initializes a new instance of the <see cref="RouterContextManager" />
    /// class.
    /// </summary>
    /// <param name="contextProvider">
    /// The <see cref="IContextProvider" /> to use for getting contexts
    /// for routing targets and activating them.
    /// </param>
    public RouterContextManager(IContextProvider contextProvider)
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

            this.currentContext = args.Context;
        };
        this.contextProvider.ContextChanged += this.onContextChanged;

        this.lazyMainContext = new Lazy<RouterContext>(() => contextProvider.ContextForTarget(Target.Main));
    }

    /// <summary>
    /// Gets the <see cref="RouterContext" /> instance for the special target
    /// <see cref="Target.Main" />.
    /// </summary>
    /// <value>
    /// The <see cref="RouterContext" /> for the special target <see cref="Target.Main" />.
    /// </value>
    /// <remarks>
    /// For a certain <see cref="Router" />, there is only one routing context
    /// for the special target <see cref="Target.Main" />, and within an
    /// application, there is usually a single top-level router.
    /// </remarks>
    private RouterContext MainContext => this.lazyMainContext.Value;

    /// <summary>
    /// Gets the router context for the specified target.
    /// </summary>
    /// <param name="target">
    /// The target for which to get the router context. This can be
    /// <see langword="null" />, <see cref="Target.Self" />, <see cref="Target.Main" />, or
    /// any other application specific target name.
    /// </param>
    /// <returns>
    /// The <see cref="RouterContext" /> for the specified target.
    /// </returns>
    /// <remarks>
    /// If the target is <see langword="null" /> or <see cref="Target.Self" />, the method
    /// returns the current active context, or the special main context if
    /// there is no active context yet.
    /// <para>
    /// If the target is <see cref="Target.Main" />, the method returns the
    /// special <c>"_main"</c> context. For any other target, the method
    /// returns a new context for the specified target, created by the route
    /// activator.
    /// </para>
    /// </remarks>
    public RouterContext GetContextForTarget(Target? target)
    {
        if (target?.IsSelf != false)
        {
            // If target is not specified or is Target.Self, then we should
            // use the current active context, unless there is no active
            // context yet. In such case, we fall back to the main context.
            return this.currentContext ?? this.MainContext;
        }

        return target.IsMain ? this.MainContext : this.contextProvider.ContextForTarget(target, this.currentContext);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        GC.SuppressFinalize(this);
        this.contextProvider.ContextChanged -= this.onContextChanged;
    }
}
