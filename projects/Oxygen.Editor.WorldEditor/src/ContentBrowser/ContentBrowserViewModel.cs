// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using System.ComponentModel;
using System.Runtime.CompilerServices;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.WorldEditor.ContentBrowser.Routing;
using IContainer = DryIoc.IContainer;

/// <summary>
/// The ViewModel for the <see cref="ContentBrowserView" /> view.
/// </summary>
/// <remarks>
/// This is an <see cref="IOutletContainer" /> with two outlets, the "left" outlet for the left pane, and the "right" outlet for
/// the right pane.
/// </remarks>
public partial class ContentBrowserViewModel : AbstractOutletContainer
{
    private static readonly Routes RoutesConfig = new(
    [
        new Route()
        {
            Path = string.Empty,
            MatchMethod = PathMatch.StrictPrefix,
            Children = new Routes(
            [
                new Route()
                {
                    Outlet = "left",
                    Path = "project",
                    ViewModelType = typeof(ProjectLayoutViewModel),
                },
                new Route()
                {
                    Outlet = "right",
                    Path = "assets",
                    ViewModelType = typeof(AssetsViewModel),
                    Children = new Routes(
                    [
                        new Route()
                        {
                            Path = "tiles",
                            ViewModelType = typeof(TilesLayoutViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);

    public ContentBrowserViewModel(IContainer container, ILoggerFactory? loggerFactory)
    {
        // Create a scoped child container for resolutions local to this content browser.
        var childContainer = container.WithRegistrationsCopy();

        var viewLocator = new DefaultViewLocator(childContainer, loggerFactory);

        // Register all local ViewModels and services in the child container
        childContainer.RegisterInstance<IViewLocator>(viewLocator);
        childContainer.RegisterInstance(new ViewModelToView(viewLocator));
        childContainer.Register<ProjectLayoutViewModel>();
        childContainer.Register<ProjectLayoutView>();
        childContainer.Register<AssetsViewModel>();
        childContainer.Register<AssetsView>();
        childContainer.Register<TilesLayoutViewModel>();
        childContainer.Register<Oxygen.Editor.WorldEditor.ContentBrowser.TilesLayoutView>();

        var context = new LocalRouterContext() { RootViewModel = this };
        var routerContextProvider = new RouterContextProvider(context);
        var router = new Router(
            RoutesConfig,
            new RouterStateManager(RoutesConfig),
            new RouterContextManager(routerContextProvider),
            new InternalRouteActivator(childContainer, loggerFactory),
            routerContextProvider,
            new DefaultUrlSerializer(new DefaultUrlParser()),
            loggerFactory);

        context.Router = router;
        context.GlobalRouter = container.Resolve<IRouter>();
        childContainer.RegisterInstance<ILocalRouterContext>(context);

        this.VmToViewConverter = childContainer.Resolve<ViewModelToView>();

        this.Outlets.Add("left", (nameof(this.LeftPaneViewModel), null));
        this.Outlets.Add("right", (nameof(this.RightPaneViewModel), null));

        router.Navigate("/(left:project~~right:assets/tiles)?path=Scenes&path=Media&filter=!folder");
    }

    public ViewModelToView VmToViewConverter { get; init; }

    public object? LeftPaneViewModel => this.Outlets["left"].viewModel;

    public object? RightPaneViewModel => this.Outlets["right"].viewModel;

    private sealed partial class LocalRouterContext()
        : RouterContext(Target.Self), ILocalRouterContext, INotifyPropertyChanged
    {
        private readonly ContentBrowserViewModel? contentBrowserViewModel;
        private IRouter? router;
        private IRouter? globalRouter;

        public event PropertyChangedEventHandler? PropertyChanged;

        public ContentBrowserViewModel RootViewModel
        {
            get => this.contentBrowserViewModel ?? throw new InvalidOperationException();
            internal init => _ = this.SetField(ref this.contentBrowserViewModel, value);
        }

        public IRouter Router
        {
            get => this.router ?? throw new InvalidOperationException();
            internal set => _ = this.SetField(ref this.router, value);
        }

        public IRouter GlobalRouter
        {
            get => this.globalRouter ?? throw new InvalidOperationException();
            internal set => _ = this.SetField(ref this.globalRouter, value);
        }

        private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
            => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

        private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
        {
            if (EqualityComparer<T>.Default.Equals(field, value))
            {
                return false;
            }

            field = value;
            this.OnPropertyChanged(propertyName);
            return true;
        }
    }
}
