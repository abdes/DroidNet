// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT).
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Views;

using CommunityToolkit.Mvvm.DependencyInjection;
using Oxygen.Editor.ViewModels;

public sealed partial class MainPage
{
    /// <summary>Initializes a new instance of the <see cref="MainPage" /> class.</summary>
    public MainPage()
    {
        this.InitializeComponent();

        this.ViewModel = Ioc.Default.GetRequiredService<MainViewModel>();
    }

    public MainViewModel ViewModel { get; }
}
