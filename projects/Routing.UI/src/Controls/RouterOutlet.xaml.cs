// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.UI.Controls;

using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.UI.Contracts;
using DroidNet.Routing.View;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

/// <summary>
/// A content control representing a router outlet; i.e. the place in a view
/// where the content of a route's view model can be rendered.
/// </summary>
[ObservableObject]
public sealed partial class RouterOutlet
{
    private static readonly DependencyProperty OutletNameProperty =
        DependencyProperty.Register(
            nameof(OutletName),
            typeof(string),
            typeof(RouterOutlet),
            new PropertyMetadata(Router.Outlet.Primary));

    private static readonly DependencyProperty VmToViewConverterProperty =
        DependencyProperty.Register(
            nameof(VmToViewConverter),
            typeof(IValueConverter),
            typeof(RouterOutlet),
            new PropertyMetadata(null));

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsActivated))]
    [NotifyPropertyChangedFor(nameof(IsActivatedWithNoContent))]
    private object? outletViewModel;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsPopulated))]
    [NotifyPropertyChangedFor(nameof(IsActivatedWithNoContent))]
    private object? outletContent;

    private IOutletContainer? outletContainer;

    public RouterOutlet()
    {
        this.InitializeComponent();

        try
        {
            this.VmToViewConverter = (IValueConverter)Application.Current.Resources["VmToViewConverter"];
        }
        catch (Exception)
        {
            // We can't find the default converter resource. Whoever is using
            // this control will have to explicitly provide a converter.
        }

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    public string OutletName
    {
        get => (string)this.GetValue(OutletNameProperty);
        set => this.SetValue(OutletNameProperty, value);
    }

    public IValueConverter VmToViewConverter
    {
        get => (IValueConverter)this.GetValue(VmToViewConverterProperty);
        set => this.SetValue(VmToViewConverterProperty, value);
    }

    public bool IsActivated => this.OutletViewModel is not null;

    public bool IsActivatedWithNoContent => this.IsActivated && !this.IsPopulated;

    public bool IsPopulated => this.OutletContent is not null;

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        /*
         * Do the work in OnLoaded/OnUnloaded because sometimes the Parent
         * property is not set until the control is loaded.
         */

        var parentView = this.FindParentView();
        if (parentView is not { ViewModel: IOutletContainer container })
        {
            throw new InvalidOperationException(
                $"you cannot use {nameof(RouterOutlet)} control outside of a view which view model implements {nameof(IOutletContainer)}");
        }

#if DEBUG
        /*
         * Try to get the observable property for our outlet name.If the
         * container does not have an outlet with our name, this call will
         * fail.
         */
        container.GetPropertyNameForOutlet(this.OutletName);
#endif

        this.outletContainer = container;
        if (this.outletContainer is INotifyPropertyChanged notifyParent)
        {
            notifyParent.PropertyChanged += this.OnParentPropertyChanged;
        }

        // Initial content update.
        this.UpdateContent();
    }

    private IViewFor? FindParentView()
    {
        var parent = this.Parent as FrameworkElement;
        while (parent is not null)
        {
            if (parent is IViewFor)
            {
                break;
            }

            parent = parent.Parent as FrameworkElement;
        }

        return parent as IViewFor;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        if (this.Parent is IViewFor { ViewModel: IOutletContainer and INotifyPropertyChanged notifyParent })
        {
            notifyParent.PropertyChanged -= this.OnParentPropertyChanged;
        }
    }

    private void OnParentPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        Debug.Assert(
            this.outletContainer is not null,
            $"expecting the outlet container to be non null inside {nameof(this.OnParentPropertyChanged)}");

        if (e.PropertyName == this.outletContainer!.GetPropertyNameForOutlet(this.OutletName))
        {
            this.UpdateContent();
        }
    }

    private void UpdateContent()
    {
        Debug.Assert(
            this.outletContainer is not null,
            $"expecting the outlet container to be non null before calling {nameof(this.UpdateContent)}");

        var vm = this.outletContainer!.GetViewModelForOutlet(this.OutletName);
        this.OutletViewModel = vm;
        this.OutletContent = vm is not null
            ? this.VmToViewConverter.Convert(this.OutletViewModel, typeof(object), null, null)
            : null;
    }
}
