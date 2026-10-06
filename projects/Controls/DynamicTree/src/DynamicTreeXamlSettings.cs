// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Settings;

namespace DroidNet.Controls;

/// <summary>Configures process-wide XAML optimizations useful for dense DynamicTree views.</summary>
public static class DynamicTreeXamlSettings
{
    /// <summary>Enables icon, style-application and deferred text-flyout optimizations without changing default templates.</summary>
    /// <remarks>Call before WinUI starts. These settings affect all controls in the process, not only DynamicTree.</remarks>
    /// <exception cref="InvalidOperationException">XAML settings have already been locked.</exception>
    public static void EnableOptimizations()
    {
        if (XamlOptionalChanges.IsLocked())
        {
            throw new InvalidOperationException("Configure DynamicTree XAML optimizations before Application.Start.");
        }

        _ = XamlOptionalChanges.EnableChange(XamlChangeId.IconNoGridOptimization);
        _ = XamlOptionalChanges.EnableChange(XamlChangeId.OptimizeApplyStyles);
        _ = XamlOptionalChanges.EnableChange(XamlChangeId.DeferContextFlyoutInit);
    }
}
