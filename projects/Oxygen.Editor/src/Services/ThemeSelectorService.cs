// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using DroidNet.Config;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Models;

public class ThemeSelectorService(IWritableOptions<ThemeSettings> settings) : IThemeSelectorService
{
    private const string SettingsKey = "AppBackgroundRequestedTheme";

    public ElementTheme Theme
    {
        get => settings.Value.AppBackgroundRequestedTheme;
        private set => settings.Value.AppBackgroundRequestedTheme = value;
    }

    public Task SetThemeAsync(ElementTheme theme)
    {
        this.Theme = theme;

        this.ApplyTheme();

        settings.Update(opt => opt.AppBackgroundRequestedTheme = theme);
        return Task.CompletedTask;
    }

    public void ApplyTheme()
    {
        // TODO: apply theme to currently active windows
        /*
        if (App.MainWindow.Content is FrameworkElement rootElement)
        {
            rootElement.RequestedTheme = this.Theme;
            TitleBarHelper.UpdateTitleBar(this.Theme);
        }
        */
    }
}
