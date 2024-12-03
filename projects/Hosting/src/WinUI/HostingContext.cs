// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Hosting.WinUI;

/// <summary>
/// Encapsulates the information needed to manage the hosting of a WinUI-based
/// User Interface service and associated thread.
/// </summary>
/// <param name="lifeTimeLinked">
/// Specifies whether the application lifetime and the UI lifetime are linked.
/// When <see langword="true" />, termination of the UI thread leads to
/// termination of the Hosted Application and vice versa.
/// </param>
public class HostingContext(bool lifeTimeLinked = true) : BaseHostingContext(lifeTimeLinked)
{
    /// <summary>
    /// Gets or sets the WinUI dispatcher queue, used to manage the execution of work items on the UI thread.
    /// </summary>
    public required DispatcherQueue Dispatcher { get; set; }

    /// <summary>
    /// Gets or sets the WinUI Application instance.
    /// </summary>
    public required Application Application { get; set; }

    /// <summary>
    /// Gets or sets the scheduler associated with the WinUI dispatcher queue.
    /// </summary>
    public required DispatcherQueueScheduler DispatcherScheduler { get; set; }
}
