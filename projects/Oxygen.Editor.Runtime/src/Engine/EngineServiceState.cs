// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Represents the lifecycle state of the <see cref="IEngineService"/> singleton.
/// </summary>
public enum EngineServiceState
{
    /// <summary>
    /// The service has been constructed but the native engine was not initialized yet.
    /// </summary>
    Created = 0,

    /// <summary>
    /// The service is currently creating and starting the native engine loop.
    /// </summary>
    Initializing,

    /// <summary>
    /// The native layer is fully configured in headless mode and awaiting viewport attachments.
    /// </summary>
    Ready,

    /// <summary>
    /// The engine loop is running and at least one viewport surface is attached.
    /// </summary>
    Running,

    /// <summary>
    /// The engine loop is stopping.
    /// </summary>
    Stopping,

    /// <summary>
    /// The service failed to start or encountered a fatal error.
    /// </summary>
    Faulted,
}
