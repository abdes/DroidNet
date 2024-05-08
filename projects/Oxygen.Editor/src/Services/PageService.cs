// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Pages.Settings.ViewModels;
using Oxygen.Editor.Pages.Settings.Views;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.ProjectBrowser.Views;
using Oxygen.Editor.ViewModels;
using Oxygen.Editor.Views;

public class PageService : IPageService
{
    private readonly Dictionary<string, Type> pages = new();

    /// <summary>Initializes a new instance of the <see cref="PageService" /> class.</summary>
    public PageService()
    {
        this.Configure<StartViewModel, StartPage>();
        this.Configure<MainViewModel, MainPage>();
        this.Configure<SettingsViewModel, SettingsPage>();
    }

    public Type GetPageType(string key)
    {
        Type? pageType;
        lock (this.pages)
        {
            if (!this.pages.TryGetValue(key, out pageType))
            {
                throw new ArgumentException(
                    $"Page not found: {key}. Did you forget to call PageService.Configure?",
                    nameof(key));
            }
        }

        return pageType;
    }

    private void Configure<VM, V>()
        where VM : ObservableObject
        where V : Page
    {
        lock (this.pages)
        {
            var key = typeof(VM).FullName!;
            if (this.pages.ContainsKey(key))
            {
                throw new InvalidOperationException($"The key {key} is already configured in PageService");
            }

            var type = typeof(V);
            if (this.pages.ContainsValue(type))
            {
                throw new InvalidOperationException(
                    $"This type is already configured with key {this.pages.First(p => p.Value == type).Key}");
            }

            this.pages.Add(key, type);
        }
    }
}
