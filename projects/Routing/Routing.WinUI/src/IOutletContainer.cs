// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.WinUI;

/// <summary>
/// Defines a contract for components that can host content in named outlets during navigation.
/// </summary>
/// <remarks>
/// <para>
/// An outlet container manages named regions (outlets) that can display content. During navigation,
/// when a route is activated, its view model is resolved to a view using a <c>ViewLocator</c>,
/// and the resulting content is loaded into the designated outlet within the container. This enables
/// complex UI layouts where different parts of the interface can be updated independently through
/// navigation.
/// </para>
///
/// <example>
/// <strong>Example Usage</strong>
/// <code><![CDATA[
/// public class ShellViewModel : IOutletContainer
/// {
///     private object? mainContent;
///     private object? sidebarContent;
///     private object? bottomContent;
///
///     // Views bound to these properties use ViewModelToView converter
///     public object? MainContent => mainContent;
///     public object? SidebarContent => sidebarContent;
///     public object? BottomContent => bottomContent;
///
///     public void LoadContent(object viewModel, OutletName? outletName = null)
///     {
///         if (outletName?.IsPrimary ?? true)
///         {
///             mainContent = viewModel;
///             OnPropertyChanged(nameof(MainContent));
///             return;
///         }
///
///         switch (outletName.Name)
///         {
///             case "sidebar":
///                 sidebarContent = viewModel;
///                 OnPropertyChanged(nameof(SidebarContent));
///                 break;
///             case "bottom":
///                 bottomContent = viewModel;
///                 OnPropertyChanged(nameof(BottomContent));
///                 break;
///             default:
///                 throw new ArgumentException($"Unknown outlet: {outletName}",
///                     nameof(outletName));
///         }
///     }
/// }
/// <!-- In XAML -->
/// <ContentPresenter Content="{x:Bind ViewModel.MainContent,
///     Converter={StaticResource VmToViewConverter}}" />
/// ]]></code>
///
/// </example>
/// <para><strong>Implementation Guidelines</strong></para>
/// <para>
/// <em>Primary Outlet Handling:</em> Every container must support a primary outlet. This is the
/// default target for content when no specific outlet is designated. In a typical layout, this
/// would be your main content area. The primary outlet should be treated as the container's default
/// content region.
/// </para>
/// <para>
/// <em>View Model Storage:</em> Maintain references to view models loaded into each outlet. These
/// references should be exposed as properties that views can bind to. Consider implementing proper
/// cleanup when view models are replaced, especially if they implement <see cref="IDisposable"/>.
/// </para>
/// <para>
/// <em>Property Change Notifications:</em> When content is loaded into any outlet, notify bound
/// views through property change events. This ensures the UI updates immediately when navigation
/// occurs. If implementing <c>INotifyPropertyChanged</c>, raise change notifications for the
/// corresponding outlet properties.
/// </para>
/// <para>
/// <em>View Resolution:</em> Ensure your views use a <see cref="DroidNet.Mvvm.Converters.ViewModelToView">
/// ViewModelToView</see> converter when binding to outlet content properties. This converter works
/// with the <see cref="DroidNet.Mvvm.IViewLocator">ViewLocator</see> to resolve the appropriate view
/// for each view model based on naming conventions or explicit mappings.
/// </para>
/// <para>
/// <em>Outlet Validation:</em> Validate outlet names during content loading. If an unknown outlet
/// is specified, throw an <see cref="ArgumentException"/> with a descriptive message. This helps
/// catch configuration errors early and provides clear feedback to developers.
/// </para>
///
/// <para><strong>Using AbstractOutletContainer</strong></para>
/// <para>For straightforward outlet containers, inherit from <see cref="AbstractOutletContainer"/>
/// which provides a robust implementation of outlet management. The concrete view model only needs
/// to initialize the outlets dictionary with mappings between outlet names and their corresponding
/// property names. The base class then handles view model storage, property change notifications,
/// and proper disposal of outlet content. This is particularly useful for containers with a fixed
/// set of outlets where each outlet's content is exposed through a dedicated property.
/// </para>
/// <code><![CDATA[
/// public class WorkspaceViewModel : AbstractOutletContainer
/// {
///     public WorkspaceViewModel()
///     {
///         Outlets.Add(OutletName.Primary, (nameof(MainContent), null));
///         Outlets.Add("sidebar", (nameof(SidebarContent), null));
///         Outlets.Add("bottom", (nameof(BottomContent), null));
///     }
///
///     public object? MainContent => Outlets[OutletName.Primary].viewModel;
///     public object? SidebarContent => Outlets["sidebar"].viewModel;
///     public object? BottomContent => Outlets["bottom"].viewModel;
/// }
/// ]]></code>
/// </remarks>
public interface IOutletContainer
{
    /// <summary>
    /// Loads a view model into the specified outlet during navigation.
    /// </summary>
    /// <param name="viewModel">The view model to be displayed in the outlet.</param>
    /// <param name="outletName">
    /// The target outlet name. Use the primary outlet when <see langword="null"/>.
    /// </param>
    /// <remarks>
    /// Called by the router during route activation. The implementation should store the view model
    /// and notify any bound views, which will then resolve the actual content using
    /// <see cref="DroidNet.Mvvm.Converters.ViewModelToView">ViewModelToView</see> converter.
    /// </remarks>
    /// <exception cref="ArgumentException">
    /// Thrown when the specified outlet name is not recognized by this container.
    /// </exception>
    void LoadContent(object viewModel, OutletName? outletName = null);
}
