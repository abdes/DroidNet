// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Pages.Settings.ViewModels;

using System.Globalization;
using System.Reflection;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Helpers;
using Oxygen.Editor.Services;
using Windows.ApplicationModel;

/// <summary>
/// A ViewModel for the Settings Page.
/// </summary>
public partial class SettingsViewModel : ObservableRecipient
{
    [ObservableProperty]
    private ElementTheme elementTheme;

    [ObservableProperty]
    private string versionDescription;

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

    public ICommand SwitchThemeCommand { get; }

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

        var sMajor = version.Major.ToString(CultureInfo.InvariantCulture);
        var sMinor = version.Minor.ToString(CultureInfo.InvariantCulture);
        var sBuild = version.Build.ToString(CultureInfo.InvariantCulture);
        var sRevision = version.Revision.ToString(CultureInfo.InvariantCulture);
        return
            $"{"AppDisplayName".GetLocalized()} - {sMajor}.{sMinor}.{sBuild}.{sRevision}";
    }
}
