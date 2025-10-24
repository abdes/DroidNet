// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using DroidNet.Aura.WindowManagement;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Docking.Demo;

/// <summary>
/// Provides application-specific behavior to supplement the default <see cref="Application" /> class.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Application class must be public")]
public partial class App
{
    private readonly IRouter router;
    private readonly IValueConverter vmToViewConverter;
    private readonly IHostApplicationLifetime lifetime;


    /// <summary>
    ///     Initializes a new instance of the <see cref="App" /> class.
    ///     <para>
    ///     In this project architecture, the single instance of the application is created by the
    ///     User Interface hosted service as part of the application host initialization. Its
    ///     lifecycle is managed together with the rest of the services.
    ///     </para>
    /// </summary>
    /// <param name="lifetime">
    ///     The host application lifetime, used to imperatively exit the application when needed.
    /// </param>
    /// <param name="router">The application router.</param>
    /// <param name="converter">
    ///     The ViewModel-to-View converter. Will be added as an App resource, to make it available
    ///     to XAML.
    /// </param>
    /// <param name="windowManager">
    ///     Window manager service - injected to force early initialization before navigation.
    /// </param>
    /// <remarks>
    ///     The <paramref name="converter" /> needs to be available in the XAML as a static
    ///     resource. However, because it has dependencies injected via the Dependency Injector, we
    ///     create it in the code behind and programmatically add it as a static resource after the
    ///     application is <see cref="OnLaunched">launched</see>.
    /// </remarks>
    public App(
        IHostApplicationLifetime lifetime,
        IRouter router,
        [FromKeyedServices("VmToView")]
        IValueConverter converter,
        IWindowManagerService windowManager)
    {
        _ = windowManager;

        this.lifetime = lifetime;
        this.router = router;
        this.vmToViewConverter = converter;
        this.InitializeComponent();
    }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        Current.Resources["VmToViewConverter"] = this.vmToViewConverter;

        // Exit if navigation fails
        _ = this.router.Events.OfType<NavigationError>().Subscribe(_ => this.lifetime.StopApplication());

        await this.router.NavigateAsync("/workspace", new FullNavigation() { Target = Target.Main }).ConfigureAwait(true);
    }
}
