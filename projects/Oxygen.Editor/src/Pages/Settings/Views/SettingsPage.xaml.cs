// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Pages.Settings.Views;

using CommunityToolkit.Mvvm.DependencyInjection;
using Oxygen.Editor.Pages.Settings.ViewModels;

// TODO: Set the URL for your privacy policy by updating SettingsPage_PrivacyTermsLink.NavigateUri in Resources.resw.
public sealed partial class SettingsPage
{
    /// <summary>Initializes a new instance of the <see cref="SettingsPage" /> class.</summary>
    public SettingsPage()
    {
        this.ViewModel = Ioc.Default.GetRequiredService<SettingsViewModel>();
        this.InitializeComponent();
    }

    public SettingsViewModel ViewModel
    {
        get;
    }
}
