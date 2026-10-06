// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Microsoft.UI.Xaml;

namespace DroidNet.Tests;

internal static class UiTestHosting
{
    internal static HostingContext CreateStatusHosting()
    {
        var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
        return new()
        {
            Application = Application.Current,
            Dispatcher = dispatcher,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };
    }
}
