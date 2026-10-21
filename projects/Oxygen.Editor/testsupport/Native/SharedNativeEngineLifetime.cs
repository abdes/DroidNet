// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using DroidNet.Tests;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>Shuts the shared native engine down once, after the last test of the process.</summary>
[TestClass]
public static class SharedNativeEngineLifetime
{
    /// <summary>Disposes the shared engine on the UI dispatcher.</summary>
    /// <returns>The cleanup task.</returns>
    [AssemblyCleanup]
    public static Task DisposeSharedEngineAsync()
        => VisualUserInterfaceTestsApp.DispatcherQueue.EnqueueAsync(SharedNativeEngine.DisposeAsync).WaitAsync(TimeSpan.FromSeconds(60));
}
