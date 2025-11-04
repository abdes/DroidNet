// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Theming;

/// <summary>
/// Provides functionality to set the application theme mode for the main window or a specific window.
/// </summary>
public interface IAppThemeModeService
{
    /// <summary>
    /// Sets application theme mode to the main window or a specific window.
    /// </summary>
    /// <param name="window">A window instance to set application theme mode.</param>
    /// <param name="requestedThemeMode">A theme mode to set.</param>
    public void ApplyThemeToWindow(Window window, ElementTheme requestedThemeMode);
}
