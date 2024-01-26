// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.WinUI;

using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

/// <summary>
/// Encapsulates the information needed to manage the hosting of a WinUI based
/// User Interface service and associated thread.
/// </summary>
public class HostingContext(bool lifeTimeLinked = true) : BaseHostingContext(lifeTimeLinked)
{
    /// <summary>Gets or sets the WinUI dispatcher queue.</summary>
    /// <value>The WinUI dispatcher queue.</value>
    public DispatcherQueue? Dispatcher { get; set; }

    /// <summary>Gets or sets the WinUI Application instance.</summary>
    /// <value>The WinUI Application instance.</value>
    public Application? Application { get; set; }
}
