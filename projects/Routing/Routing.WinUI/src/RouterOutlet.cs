// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Routing.WinUI;

/// <summary>
/// A user control that represents a router outlet - a designated region in the UI where routed
/// content is dynamically loaded and displayed during navigation.
/// </summary>
/// <remarks>
/// <para>
/// The RouterOutlet serves as the visual manifestation of routing in your application's UI.
/// When routes are activated, their associated view models are loaded into outlets, which then
/// resolve and display the corresponding views. This control handles the view resolution process
/// automatically using a configured view model to view converter.
/// </para>
/// <para>
/// The outlet automatically handles View resolution through the application-wide <see cref="VmToViewConverter"/>,
/// which converts view models to their corresponding views using naming conventions or explicit mappings.
/// It also properly manage the content lifecycle when the ViewModel changes, and uses visual states
/// to show appropriate feedback when the outlet is inactive or when view resolution fails.
/// </para>
///
/// <strong>Setup Requirements</strong>
/// <para>
/// 1. Register the view model to view converter in your application resources, using the
/// <c>Application</c> resource dictionary or code behind:
/// </para>
/// <code><![CDATA[
/// <Application.Resources>
///     <converters:ViewModelToView x:Key="VmToViewConverter"/>
/// </Application.Resources>
/// ]]></code>
/// You can also set the converter directly on the outlet, using iys <see cref="VmToViewConverter"/>
/// property, if you prefer not to use resources.
/// <para>
/// 2. Ensure your view models are properly configured in the router configuration and
/// their corresponding views are registered with the dependency injection container.
/// </para>
///
/// <para><strong>Error Handling</strong></para>
/// <para>
/// The outlet provides clear visual feedback when issues occur, such as missing view
/// registrations or converter configuration problems. This helps developers quickly
/// identify and resolve routing-related issues during development.
/// </para>
/// </remarks>
///
/// <example>
/// <strong>Example Usage</strong>
/// Here's how to use RouterOutlet in a shell view that hosts routed content:
/// <code><![CDATA[
/// <Grid>
///     <Grid.RowDefinitions>
///         <RowDefinition Height="Auto"/>
///         <RowDefinition Height="*"/>
///     </Grid.RowDefinitions>
///
///     <!-- Navigation header -->
///     <NavigationBar Grid.Row="0"/>
///
///     <!-- Main content area (uses primary outlet) -->
///     <router:RouterOutlet
///         Outlet=""
///         Grid.Row="1"
///         ViewModel="{x:Bind ViewModel.ContentViewModel, Mode=OneWay}"
///         VmToViewConverter="{StaticResource VmToViewConverter}"/>
/// </Grid>
/// ]]></code>
/// </example>
[TemplateVisualState(Name = ErrorVisualState, GroupName = OutletVisualStates)]
[TemplateVisualState(Name = InactiveVisualState, GroupName = OutletVisualStates)]
[TemplateVisualState(Name = NormalVisualState, GroupName = OutletVisualStates)]
[TemplatePart(Name = RootGrid, Type = typeof(Grid))]
[TemplatePart(Name = ContentPresenter, Type = typeof(ContentPresenter))]
[ObservableObject]
public sealed partial class RouterOutlet : ContentControl
{
    /// <summary>
    /// The name of the visual state group that contains the visual states for the outlet.
    /// </summary>
    public const string OutletVisualStates = "OutletStates";

    /// <summary>
    /// The name of the visual state that represents the normal state of the outlet.
    /// </summary>
    public const string NormalVisualState = "Normal";

    /// <summary>
    /// The name of the visual state that represents the inactive state of the outlet.
    /// </summary>
    public const string InactiveVisualState = "Inactive";

    /// <summary>
    /// The name of the visual state that represents the error state of the outlet.
    /// </summary>
    public const string ErrorVisualState = "Error";

    /// <summary>
    /// The name of the root grid part in the control template.
    /// </summary>
    public const string RootGrid = "PartRootGrid";

    /// <summary>
    /// The name of the content presenter part in the control template.
    /// </summary>
    public const string ContentPresenter = "PartContentPresenter";

    /// <summary>
    /// Identifies the <see cref="ViewModel"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ViewModelProperty =
        DependencyProperty.Register(
            nameof(ViewModel),
            typeof(object),
            typeof(RouterOutlet),
            new PropertyMetadata(default, OnNeedUpdateContent));

    /// <summary>
    /// Identifies the <see cref="VmToViewConverter"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty VmToViewConverterProperty =
        DependencyProperty.Register(
            nameof(VmToViewConverter),
            typeof(IValueConverter),
            typeof(RouterOutlet),
            new PropertyMetadata(default(IValueConverter), OnNeedUpdateContent));

    /// <summary>
    /// Identifies the <see cref="OutletContent"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty OutletContentProperty =
        DependencyProperty.Register(
            nameof(OutletContent),
            typeof(UIElement),
            typeof(RouterOutlet),
            new PropertyMetadata(default(UIElement)));

    private string? outlet;
    private Grid? rootGrid;

    /// <summary>
    /// Initializes a new instance of the <see cref="RouterOutlet"/> class.
    /// </summary>
    /// <remarks>
    /// Sets up property change handling to ensure content is updated when either the view model
    /// or converter changes. Also attempts to locate the converter in application resources if
    /// not explicitly provided.
    /// </remarks>
    public RouterOutlet()
    {
        this.DefaultStyleKey = typeof(RouterOutlet);
    }

    /// <summary>
    /// Gets or sets the view model for the router outlet.
    /// </summary>
    public object? ViewModel
    {
        get => this.GetValue(ViewModelProperty);
        set => this.SetValue(ViewModelProperty, value);
    }

    /// <summary>
    /// Gets or sets the converter used to convert view models to views.
    /// </summary>
    public IValueConverter? VmToViewConverter
    {
        get => (IValueConverter)this.GetValue(VmToViewConverterProperty);
        set
        {
            if (value is null)
            {
                this.UseDefaultVmToViewConverter();
            }
            else
            {
                this.SetValue(VmToViewConverterProperty, value);
            }
        }
    }

    /// <summary>
    /// Gets or sets the name of this outlet, used to identify it during navigation.
    /// </summary>
    /// <remarks>
    /// This is not a dependency property, as it is primarily used for debugging and is not expected
    /// to change at runtime. The logic of associated outlets with routes is handled by the router.
    /// </remarks>
    public string Outlet
    {
        get => this.outlet ?? "__not_set__";
        set => this.outlet = value.Length == 0 ? "_primary_" : value;
    }

    /// <summary>
    /// Gets or sets the content to be displayed in the outlet.
    /// </summary>
    /// <remarks>
    /// This property holds the UI element that is displayed in the outlet. It is set by converting the
    /// <see cref="ViewModel"/> using the <see cref="VmToViewConverter"/>. The content is then displayed
    /// in the <see cref="ContentPresenter"/> of the control template.
    /// </remarks>
    internal UIElement? OutletContent
    {
        get => (UIElement)this.GetValue(OutletContentProperty);
        set => this.SetValue(OutletContentProperty, value);
    }

    /// <inheritdoc/>
    protected override void OnApplyTemplate()
    {
        if (this.VmToViewConverter is null)
        {
            this.UseDefaultVmToViewConverter();
        }

        this.rootGrid = this.GetTemplateChild(RootGrid) as Grid
            ?? throw new InvalidOperationException($"using an invalid template (missing {RootGrid}) for the {nameof(RouterOutlet)} control");

        this.UpdateContent();

        base.OnApplyTemplate();
    }

    private static void OnNeedUpdateContent(DependencyObject d, DependencyPropertyChangedEventArgs args)
    {
        if (d is RouterOutlet outlet && !Equals(args.OldValue, args.NewValue))
        {
            if (outlet.DispatcherQueue.HasThreadAccess)
            {
                outlet.UpdateContent();
            }
            else
            {
                _ = outlet.DispatcherQueue.TryEnqueue(outlet.UpdateContent);
            }
        }
    }

    private void UseDefaultVmToViewConverter()
    {
        if (!Application.Current.Resources.TryGetValue("VmToViewConverter", out var converter))
        {
            return;
        }

        Debug.WriteLine($"Using application default {nameof(this.VmToViewConverter)} for {nameof(RouterOutlet)} name=`{this.Outlet}` vm={this.ViewModel}");
        this.SetValue(VmToViewConverterProperty, converter);
    }

    /// <summary>
    /// Updates the outlet's content by resolving the view for the current view model.
    /// </summary>
    private void UpdateContent()
    {
        this.OutletContent = this.VmToViewConverter?.Convert(
            this.ViewModel,
            typeof(object),
            parameter: null,
            language: null) as UIElement;

        // Visual states are defined under the root grid, which is only there once the template is applied.
        if (this.rootGrid is not null)
        {
            this.UpdateVisualStates();
        }
    }

    /// <summary>
    /// Updates the visual states of the outlet based on the current content and view model.
    /// </summary>
    private void UpdateVisualStates()
    {
        var state = this switch
        {
            { ViewModel: null } => InactiveVisualState,
            { OutletContent: null } => ErrorVisualState,
            _ => NormalVisualState,
        };
        try
        {
            var result = VisualStateManager.GoToState(this, state, useTransitions: true);
            Debug.Assert(result, $"Failed to go to visual state `{state}`.");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Exception thrown while trying to go to visual state `{state}`: {ex}");
            throw;
        }
    }
}
