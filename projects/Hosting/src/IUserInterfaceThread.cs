// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting;

/// <summary>
/// Represents a user interface thread in a hosted application.
/// </summary>
public interface IUserInterfaceThread
{
    /// <summary>
    /// Starts the User Interface thread.
    /// </summary>
    /// <remarks>
    /// Note that after calling this method, the thread may not be actually running. To check if
    /// that is the case or not, use the <see cref="BaseHostingContext.IsRunning" /> property.
    /// </remarks>
    public void StartUserInterface();

    /// <summary>
    /// Asynchronously requests the User Interface thread to stop.
    /// </summary>
    /// <returns>
    /// A task that represents the asynchronous operation. The task will complete when the User
    /// Interface thread has stopped.
    /// </returns>
    public Task StopUserInterfaceAsync();
}
