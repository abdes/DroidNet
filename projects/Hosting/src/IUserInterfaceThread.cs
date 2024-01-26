// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting;

/// <summary>
/// Represents a user interface thread in a hosted application.
/// </summary>
public interface IUserInterfaceThread
{
    /// <summary>Starts the User Interface thread.</summary>
    /// <remarks>
    /// Note that after calling this method, the thread may not be actually
    /// running. To check if that is the case or not use the
    /// <see cref="BaseHostingContext.IsRunning" />.
    /// </remarks>
    void StartUserInterface();

    /// <summary>
    /// Asynchronously request the User Interface thread to stop.
    /// </summary>
    /// <returns>
    /// The asynchronous task on which to wait for completion.
    /// </returns>
    Task StopUserInterfaceAsync();
}
