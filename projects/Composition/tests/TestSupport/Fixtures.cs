// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Composition.Tests.TestSupport;

/// <summary>
///     The layers and services of the test chain root &gt; session &gt; window &gt; panel. Tests import
///     them with <c>using static</c>.
/// </summary>
internal static class Fixtures
{
    /// <summary>The root layer of the test chain root &gt; session &gt; window &gt; panel.</summary>
    [SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker")]
    public interface IRootLayer : IScopeLayer
    {
    }

    /// <summary>The session layer of the test chain.</summary>
    [SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker")]
    public interface ISessionLayer : IScopeLayer
    {
    }

    /// <summary>The window layer of the test chain.</summary>
    [SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker")]
    public interface IWindowLayer : IScopeLayer
    {
    }

    /// <summary>The panel layer of the test chain.</summary>
    [SuppressMessage("Design", "CA1040:Avoid empty interfaces", Justification = "a layer is a compile-time marker")]
    public interface IPanelLayer : IScopeLayer
    {
    }

    /// <summary>A service the tests register at one layer and resolve from another.</summary>
    public interface IService
    {
    }

    /// <summary>A service with an open generic registration.</summary>
    /// <typeparam name="T">The type argument.</typeparam>
    public interface IGenericService<T>
    {
    }

    /// <summary>A service no scope registers.</summary>
    public interface IMissingService
    {
    }

    /// <summary>Records the order in which the tests' objects are created and disposed.</summary>
    public sealed class EventLog
    {
        private readonly Lock gate = new();
        private readonly List<string> events = [];

        /// <summary>Gets the recorded events, in order.</summary>
        public IReadOnlyList<string> Events
        {
            get
            {
                lock (this.gate)
                {
                    return [.. this.events];
                }
            }
        }

        /// <summary>Records an event.</summary>
        /// <param name="name">The event.</param>
        public void Add(string name)
        {
            lock (this.gate)
            {
                this.events.Add(name);
            }
        }
    }

    /// <summary>An implementation of <see cref="IService" /> that records its disposal.</summary>
    public sealed class Service : IService, IDisposable
    {
        /// <summary>Gets a value indicating whether the service has been disposed.</summary>
        public bool IsDisposed { get; private set; }

        /// <inheritdoc />
        public void Dispose() => this.IsDisposed = true;
    }

    /// <summary>A second implementation of <see cref="IService" />, which a scope registers to shadow the first.</summary>
    public sealed class OtherService : IService
    {
    }

    /// <summary>The implementation of <see cref="IGenericService{T}" />.</summary>
    /// <typeparam name="T">The type argument.</typeparam>
    public sealed class GenericService<T> : IGenericService<T>
    {
    }

    /// <summary>A transient that depends on <see cref="IService" />.</summary>
    /// <param name="service">The service.</param>
    public sealed class Consumer(IService service)
    {
        /// <summary>Gets the service the consumer received.</summary>
        public IService Service { get; } = service;
    }

    /// <summary>A scope owner: it creates its child scopes under the scope that builds it.</summary>
    /// <param name="scope">The handle of the scope that builds the owner.</param>
    public sealed class ScopeOwner(ScopeHandle scope)
    {
        /// <summary>Gets the handle the owner received.</summary>
        public ScopeHandle Scope { get; } = scope;
    }

    /// <summary>A service with an optional dependency that no scope registers.</summary>
    /// <param name="missing">The optional dependency.</param>
    public sealed class OptionalConsumer(IMissingService? missing = null)
    {
        /// <summary>Gets the optional dependency the consumer received.</summary>
        public IMissingService? Missing { get; } = missing;
    }

    /// <summary>A service that records its creation and disposal in an <see cref="EventLog" />.</summary>
    public abstract class Recorder : IDisposable
    {
        private readonly EventLog log;
        private readonly string name;

        /// <summary>Initializes a new instance of the <see cref="Recorder" /> class.</summary>
        /// <param name="log">The log.</param>
        /// <param name="name">The name recorded with each event.</param>
        protected Recorder(EventLog log, string name)
        {
            this.log = log;
            this.name = name;
            log.Add($"created {name}");
        }

        /// <inheritdoc />
        public void Dispose() => this.log.Add($"disposed {this.name}");
    }

    /// <summary>The first recorder of an activation list.</summary>
    /// <param name="log">The log.</param>
    public sealed class FirstRecorder(EventLog log) : Recorder(log, "first");

    /// <summary>The second recorder of an activation list.</summary>
    /// <param name="log">The log.</param>
    public sealed class SecondRecorder(EventLog log) : Recorder(log, "second");

    /// <summary>A recorder whose name the test chooses.</summary>
    /// <param name="log">The log.</param>
    /// <param name="name">The name recorded with each event.</param>
    public sealed class NamedRecorder(EventLog log, string name) : Recorder(log, name);

    /// <summary>A service whose construction fails.</summary>
    public sealed class FailingService
    {
        /// <summary>Initializes a new instance of the <see cref="FailingService" /> class.</summary>
        /// <exception cref="InvalidOperationException">Always.</exception>
        public FailingService() => throw new InvalidOperationException("activation failure");
    }

    /// <summary>An async disposable that records its disposal in an <see cref="EventLog" />.</summary>
    /// <param name="log">The log.</param>
    /// <param name="name">The name recorded with the event.</param>
    public sealed class AsyncRecorder(EventLog log, string name) : IAsyncDisposable
    {
        /// <inheritdoc />
        public ValueTask DisposeAsync()
        {
            log.Add($"disposed {name}");
            return ValueTask.CompletedTask;
        }
    }
}
