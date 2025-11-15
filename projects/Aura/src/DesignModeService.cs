// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Windows.ApplicationModel;

namespace DroidNet.Aura;

/// <summary>
///     Provides helper utilities for detecting whether the application is running inside a
///     designer surface (Visual Studio/XAML Designer).
/// </summary>
internal static class DesignModeService
{
    /// <summary>
    ///     Gets a value indicating whether the application is running under a design mode host.
    /// </summary>
    public static bool IsDesignModeEnabled { get; } = DetectDesignEnvironment();

    /// <summary>
    ///     Gets a value indicating whether Visual Studio visual diagnostics (Live Visual Tree, Hot Reload, etc.)
    ///     are attached to the current process.
    /// </summary>
    public static bool IsVisualDiagnosticsAttached { get; } = DetectVisualDiagnostics();

    private static bool DetectDesignEnvironment()
    {
        if (DesignMode.DesignModeEnabled || DesignMode.DesignMode2Enabled)
        {
            return true;
        }

        return false;
    }

    private static bool DetectVisualDiagnostics()
    {
        if (IsDesignModeEnabled)
        {
            return true;
        }

        if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("VSAPPIDNAME"))
            || !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("VisualStudioEdition"))
            || !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("VS_DEBUGGER_PROC_NAME")))
        {
            return true;
        }

        // Visual Studio Live Visual Tree / XAML Diagnostics load Microsoft.VisualStudio.DesignTools.* modules
        try
        {
            using var process = Process.GetCurrentProcess();
            foreach (ProcessModule module in process.Modules)
            {
                var moduleName = module.ModuleName;
                if (moduleName.StartsWith("Microsoft.VisualStudio.DesignTools", StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
        }
#pragma warning disable CA1031 // Best effort detection; failures fall back to runtime checks.
        catch
        {
            // Ignore failures enumerating modules (possible in restricted environments).
        }
#pragma warning restore CA1031

        return false;
    }
}
