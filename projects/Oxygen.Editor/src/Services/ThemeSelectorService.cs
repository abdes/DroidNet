// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using DroidNet.Config;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Helpers;
using Oxygen.Editor.Models;

public class ThemeSelectorService : IThemeSelectorService
{
    private const string SettingsKey = "AppBackgroundRequestedTheme";
    private readonly IWritableOptions<ThemeSettings> settings;

    /// <summary>
    /// Initializes a new instance of the <see cref="ThemeSelectorService" />
    /// class.
    /// </summary>
    /// <param name="settings"></param>
    public ThemeSelectorService(IWritableOptions<ThemeSettings> settings)
        => this.settings = settings;

    public ElementTheme Theme
    {
        get => this.settings.Value.AppBackgroundRequestedTheme;
        private set => this.settings.Value.AppBackgroundRequestedTheme = value;
    }

    public Task SetThemeAsync(ElementTheme theme)
    {
        this.Theme = theme;

        this.ApplyTheme();

        this.settings.Update(
            opt =>
            {
                opt.AppBackgroundRequestedTheme = theme;
            });
        return Task.CompletedTask;
    }

    public void ApplyTheme()
    {
        if (App.MainWindow.Content is FrameworkElement rootElement)
        {
            rootElement.RequestedTheme = this.Theme;
            TitleBarHelper.UpdateTitleBar(this.Theme);
        }
    }
}
