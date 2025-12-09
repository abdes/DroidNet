// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Represents the lifecycle state of the <see cref="IEngineService"/> singleton.
/// </summary>
public enum EngineServiceState
{
    /// <summary>
    ///     No runtime engine has not been created yet, or the service has been already disposed.
    /// </summary>
    NoEngine = 0,

    /// <summary>
    ///     The service is currently creating and initializing the runtime engine. Upon completion,
    ///     the service will transition to either the <see cref="Ready"/> state on success, or the
    ///     <see cref="Faulted"/> state on failure.
    /// </summary>
    /// <remarks>
    ///    This is a transient state that takes some time, and while in it, no operations on the
    ///    engine are allowed.
    /// </remarks>
    Initializing,

    /// <summary>
    ///     The runtime engine is initialized, and ready to be started. At this point, certain
    ///     properties may be queried and modified, and certain operations may be performed, but the
    ///     engine loop is not yet running until <see cref="IEngineService.StartAsync"/> is called.
    /// </summary>
    Ready,

    /// <summary>
    ///     The engine frame loop is starting. Upon completion, the service will transition to the
    ///     <see cref="Running"/> state on success and to the <see cref="Faulted"/> state on failure.
    /// </summary>
    Starting,

    /// <summary>
    ///     The engine is running. You may call <see cref="IEngineService.ShutdownAsync"/> to shut
    ///     it down and return to the <see cref="NoEngine"/> state.
    /// </summary>
    Running,

    /// <summary>
    ///     The engine is in the process of shutting down. Upon completion, the service will
    ///     transition back to the <see cref="NoEngine"/> state on success and to the <see
    ///     cref="Faulted"/> state on failure.
    /// </summary>
    /// <remarks>
    ///    This is a transient state that takes some time, and while in it, no operations on the
    ///    engine are allowed.
    /// </remarks>
    ShuttingDown,

    /// <summary>
    ///     The engine reported a fatal error. No further operations other than <see
    ///     cref="IEngineService.InitializeAsync"/> to re-create and re-attempt initialize the
    ///     engine may be performed.
    /// </summary>
    Faulted,
}
