// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Pages.Settings.ViewModels;

using System.Reflection;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Helpers;
using Oxygen.Editor.Services;
using Windows.ApplicationModel;

public partial class SettingsViewModel : ObservableRecipient
{
    [ObservableProperty]
    private ElementTheme elementTheme;

    [ObservableProperty]
    private string versionDescription;

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsViewModel" />
    /// class.
    /// </summary>
    public SettingsViewModel(IThemeSelectorService themeSelectorService)
    {
        this.elementTheme = themeSelectorService.Theme;
        this.versionDescription = GetVersionDescription();

        this.SwitchThemeCommand = new AsyncRelayCommand<ElementTheme>(
            async (param) =>
            {
                if (this.ElementTheme != param)
                {
                    this.ElementTheme = param;
                    await themeSelectorService.SetThemeAsync(param).ConfigureAwait(false);
                }
            });
    }

    public ICommand SwitchThemeCommand
    {
        get;
    }

    private static string GetVersionDescription()
    {
        Version version;

        if (RuntimeHelper.IsMSIX)
        {
            var packageVersion = Package.Current.Id.Version;

            version = new Version(
                packageVersion.Major,
                packageVersion.Minor,
                packageVersion.Build,
                packageVersion.Revision);
        }
        else
        {
            version = Assembly.GetExecutingAssembly()
                .GetName()
                .Version!;
        }

        return
            $"{"AppDisplayName".GetLocalized()} - {version.Major}.{version.Minor}.{version.Build}.{version.Revision}";
    }
}
