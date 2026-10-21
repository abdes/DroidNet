// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Runtime.ExceptionServices;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>
/// Passes everything to a real engine and remembers the last scene activation it accepted, so a test can observe the
/// scene the preview shows through the engine's own observation commands.
/// </summary>
public class EngineProbe : DispatchProxy
{
    private static readonly MethodInfo WorldCommandsGetter = typeof(IEngineService).GetProperty(nameof(IEngineService.WorldCommands))!.GetMethod!;

    private readonly Dictionary<Delegate, Delegate> handlers = [];
    private IEngineService inner = null!;
    private IRuntimeWorldCommands? wrappedFor;
    private IRuntimeWorldCommands? wrapped;

    /// <summary>Occurs when the engine accepts a scene activation.</summary>
    public event EventHandler? Activated;

    /// <summary>Gets the last scene activation the engine accepted.</summary>
    public RuntimeSceneTarget? LastActivated { get; private set; }

    /// <summary>Creates the probe of an engine.</summary>
    /// <param name="engine">The real engine.</param>
    /// <returns>The engine the preview uses, and the probe that watches it.</returns>
    public static (IEngineService Engine, EngineProbe Probe) Of(IEngineService engine)
    {
        var proxy = Create<IEngineService, EngineProbe>();
        var probe = (EngineProbe)(object)proxy;
        probe.inner = engine;
        return (proxy, probe);
    }

    /// <inheritdoc/>
    protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
    {
        ArgumentNullException.ThrowIfNull(targetMethod);
        if (targetMethod == WorldCommandsGetter)
        {
            // The preview compares world instances, so one engine world keeps one probe.
            var commands = this.inner.WorldCommands;
            if (!ReferenceEquals(commands, this.wrappedFor))
            {
                this.wrappedFor = commands;
                this.wrapped = WorldProbe.Of(commands, this);
            }

            return this.wrapped;
        }

        return Forward(this.inner, (IEngineService)(object)this, targetMethod, args, this.handlers);
    }

    // Passes a call on; an event handler subscribes through a wrapper that names the proxy as the sender, as the
    // subscriber compares the sender with the instance it subscribed to.
    private static object? Forward(object target, object proxy, MethodInfo method, object?[]? args, Dictionary<Delegate, Delegate> handlers)
    {
        if (args is [Delegate handler] && (method.Name.StartsWith("add_", StringComparison.Ordinal) || method.Name.StartsWith("remove_", StringComparison.Ordinal)))
        {
            Delegate? wrapper;
            lock (handlers)
            {
                if (method.Name.StartsWith("add_", StringComparison.Ordinal))
                {
                    wrapper = SenderAs(handler, proxy);
                    handlers[handler] = wrapper;
                }
                else if (!handlers.Remove(handler, out wrapper))
                {
                    return null;
                }
            }

            args = [wrapper];
        }

        try
        {
            return method.Invoke(target, args);
        }
        catch (TargetInvocationException exception) when (exception.InnerException is { } inner)
        {
            ExceptionDispatchInfo.Throw(inner);
            throw;
        }
    }

    private static Delegate SenderAs(Delegate handler, object proxy)
    {
        var type = handler.GetType();
        if (type == typeof(EventHandler))
        {
            var plain = (EventHandler)handler;
            return new EventHandler((_, args) => plain(proxy, args));
        }

        var argsType = type.GetGenericArguments().Single();
        return (Delegate)typeof(EngineProbe).GetMethod(nameof(SenderAsTyped), BindingFlags.NonPublic | BindingFlags.Static)!
            .MakeGenericMethod(argsType).Invoke(obj: null, [handler, proxy])!;
    }

    private static EventHandler<TArgs> SenderAsTyped<TArgs>(EventHandler<TArgs> handler, object proxy)
        => (_, args) => handler(proxy, args);

    private void OnActivated(RuntimeSceneTarget target)
    {
        this.LastActivated = target;
        this.Activated?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Passes the world commands through and reports each accepted activation to its engine probe.</summary>
    public class WorldProbe : DispatchProxy
    {
        private readonly Dictionary<Delegate, Delegate> handlers = [];
        private IRuntimeWorldCommands inner = null!;
        private EngineProbe owner = null!;

        internal static IRuntimeWorldCommands Of(IRuntimeWorldCommands commands, EngineProbe owner)
        {
            var proxy = Create<IRuntimeWorldCommands, WorldProbe>();
            var probe = (WorldProbe)(object)proxy;
            probe.inner = commands;
            probe.owner = owner;
            return proxy;
        }

        /// <inheritdoc/>
        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            ArgumentNullException.ThrowIfNull(targetMethod);
            var result = Forward(this.inner, this, targetMethod, args, this.handlers);
            return string.Equals(targetMethod.Name, nameof(IRuntimeWorldCommands.ActivateSceneAsync), StringComparison.Ordinal)
                ? this.ReportAsync((Task<RuntimeCommandResult>)result!, (RuntimeSceneTarget)args![1]!)
                : result;
        }

        private async Task<RuntimeCommandResult> ReportAsync(Task<RuntimeCommandResult> activation, RuntimeSceneTarget target)
        {
            var result = await activation.ConfigureAwait(true);
            if (result.Succeeded)
            {
                this.owner.OnActivated(target);
            }

            return result;
        }
    }
}
