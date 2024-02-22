// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger;

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// Provides application-specific behavior to supplement the default Application
/// class.
/// </summary>
public partial class App
{
    private readonly IRouter router;
    private readonly IValueConverter vmToViewConverter;
    private readonly IHostApplicationLifetime lifetime;

    /// <summary>
    /// Initializes a new instance of the <see cref="App" /> class.
    /// </summary>
    /// In this project architecture, the single instance of the application is
    /// created by the User Interface hosted service as part of the application
    /// host initialization. Its lifecycle is managed together with the rest of
    /// the services.
    /// <param name="lifetime">
    /// The host application lifetime, used to
    /// imperatively exit the application when needed.
    /// </param>
    /// <param name="router">The application router.</param>
    /// <param name="converter">
    /// The ViewModel to View converter to be used to set the content inside
    /// the content control.
    /// </param>
    /// <remarks>
    /// The <paramref name="converter" /> needs to be available in the XAML as
    /// a static resource. However, because it has dependencies injected via
    /// the Dependency Injector, we create it in the code behind and
    /// programmatically add it as a static resource after the application is
    /// <see cref="OnLaunched">launched</see>.
    /// </remarks>
    public App(
        IHostApplicationLifetime lifetime,
        IRouter router,
        [FromKeyedServices("VmToView")]
        IValueConverter converter)
    {
        this.lifetime = lifetime;
        this.router = router;
        this.vmToViewConverter = converter;
        this.InitializeComponent();
    }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Current.Resources["VmToViewConverter"] = this.vmToViewConverter;

        try
        {
            this.router.Navigate(
                "/(dock:(app:Welcome//routes:Config/Routes;minimized;left//url-tree:Parser/UrlTree;left//routes-top:Config/Routes;top//router-state:Router/State;right//routes-bottom:Config/Routes;minimized;bottom//router-state-m:Router/State;minimized;right//routes-top-m:Config/Routes;minimized;top))",
                new FullNavigation() { Target = Target.Main });
        }
        catch (NavigationFailedException)
        {
            this.lifetime.StopApplication();
        }

        // "/(app:Welcome//dock:(1:One;lef;pinned//2:Two;below=1//3:Three;left;pinned//4:Four;bottom))",

        // "/(app:Blank//dock:left(routes:Config/Routes;pinned))",
        // "/(app:Home/Welcome//dock:left(1:One;pinned//2:Two;below=1//3:Three;pinned;above=2//4:Four))",
        // "/DockTest",
    }
}
