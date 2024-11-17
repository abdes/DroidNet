// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
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
///     <!-- Main content area -->
///     <router:RouterOutlet
///         Grid.Row="1"
///         ViewModel="{x:Bind ViewModel.ContentViewModel, Mode=OneWay}"
///         VmToViewConverter="{StaticResource VmToViewConverter}"/>
/// </Grid>
/// ]]></code>
/// </example>
[ObservableObject]
public sealed partial class RouterOutlet
{
    /// <summary>
    /// Gets or sets the resolved view content for this outlet.
    /// </summary>
    /// <remarks>
    /// This property is automatically updated when either the <see cref="ViewModel"/> or
    /// <see cref="VmToViewConverter"/> changes. It represents the actual UI content
    /// displayed in the outlet.
    /// </remarks>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsPopulated))]
    [NotifyPropertyChangedFor(nameof(IsActivatedWithNoContent))]
    private object? outletContent;

    /// <summary>
    /// Gets or sets the view model whose content should be displayed in this outlet.
    /// </summary>
    /// <remarks>
    /// When this property changes, the outlet automatically resolves the corresponding view
    /// using the configured <see cref="VmToViewConverter"/> and updates its content.
    /// </remarks>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsActivated))]
    [NotifyPropertyChangedFor(nameof(IsActivatedWithNoContent))]
    private object? viewModel;

    /// <summary>
    /// Gets or sets the converter used to resolve views from view models.
    /// </summary>
    /// <remarks>
    /// If not explicitly set, the outlet will attempt to locate a converter with the key
    /// "VmToViewConverter" in the application resources when the control is loaded.
    /// </remarks>
    [ObservableProperty]
    private IValueConverter? vmToViewConverter;

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
        this.InitializeComponent();

        // Listen for future changes to the ViewModel or the VmToViewConverter
        // properties to update the outlet content as soon as possible.
        this.PropertyChanged += this.OnNeedUpdateContent;

        this.Loaded += (_, _) =>
        {
            if (this.VmToViewConverter is null)
            {
                if (!Application.Current.Resources.TryGetValue("VmToViewConverter", out var converter))
                {
                    return;
                }

                this.vmToViewConverter = (IValueConverter)converter;
            }

            // Update the content on Loaded only if it has not been already updated.
            if (this.OutletContent is null)
            {
                this.UpdateContent();
            }
        };

        this.Unloaded += (_, _) => this.PropertyChanged -= this.OnNeedUpdateContent;
    }

    /// <summary>
    /// Gets or sets the name of this outlet, used to identify it during navigation.
    /// </summary>
    /// <remarks>
    /// When specified, this outlet will only display content targeted for this name in the
    /// route configuration. If not specified, the outlet accepts content for the primary outlet.
    /// </remarks>
    public string? Outlet { get; set; }

    /// <summary>
    /// Gets a value indicating whether this outlet has an assigned view model.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if <see cref="ViewModel"/> is not <see langword="null"/>;
    /// otherwise, <see langword="false"/>.
    /// </value>
    public bool IsActivated => this.ViewModel is not null;

    /// <summary>
    /// Gets a value indicating whether this outlet has a view model but no resolved content.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if <see cref="ViewModel"/> is not <see langword="null"/> but
    /// <see cref="OutletContent"/> is <see langword="null"/>; otherwise, <see langword="false"/>.
    /// </value>
    /// <remarks>
    /// This state typically indicates a view resolution failure and can be used to show
    /// appropriate error UI.
    /// </remarks>
    public bool IsActivatedWithNoContent => this.IsActivated && !this.IsPopulated;

    /// <summary>
    /// Gets a value indicating whether this outlet has resolved content.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if <see cref="OutletContent"/> is not <see langword="null"/>;
    /// otherwise, <see langword="false"/>.
    /// </value>
    public bool IsPopulated => this.OutletContent is not null;

    /// <summary>
    /// Handles property changes that require content updates.
    /// </summary>
    /// <param name="sender">The source of the property change event.</param>
    /// <param name="args">Event data identifying which property changed.</param>
    /// <remarks>
    /// Triggers content resolution when either the <see cref="ViewModel"/> or
    /// <see cref="VmToViewConverter"/> properties change.
    /// </remarks>
    private void OnNeedUpdateContent(object? sender, PropertyChangedEventArgs args)
    {
        _ = sender; // unused

        if (args.PropertyName is not null &&
            (args.PropertyName.Equals(nameof(this.ViewModel), StringComparison.Ordinal) ||
             args.PropertyName.Equals(nameof(this.VmToViewConverter), StringComparison.Ordinal)))
        {
            this.UpdateContent();
        }
    }

    /// <summary>
    /// Updates the outlet's content by resolving the view for the current view model.
    /// </summary>
    /// <remarks>
    /// Uses the configured converter to resolve the view model to its corresponding view.
    /// If resolution fails, clears the outlet content while maintaining the activated state.
    /// </remarks>
    private void UpdateContent()
    {
        if (this.ViewModel is null)
        {
            return;
        }

        if (this.VmToViewConverter is null)
        {
            return;
        }

        this.OutletContent = this.VmToViewConverter.Convert(
            this.ViewModel,
            typeof(object),
            parameter: null,
            language: null);
    }
}
