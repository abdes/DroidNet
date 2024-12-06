// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Routing.WinUI;

/// <summary>
/// Provides a base implementation for view models that host content in multiple outlets.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="AbstractOutletContainer"/> simplifies outlet container implementation by managing
/// outlet registration, content loading, and view model lifecycle. Derived classes only need to
/// initialize the outlets dictionary and expose properties for binding to views.
/// </para>
/// <para><strong>Implementation Notes</strong></para>
/// <para>
/// The base class handles view model disposal, property change notifications, and outlet validation.
/// Derived classes only need to focus on defining outlets and exposing their content through
/// properties. The outlet dictionary maps outlet names to property names, enabling automatic
/// property updates when content changes.
/// </para>
/// </remarks>
/// <example>
/// <strong>Example Implementation</strong>
/// <para>
/// Here's a workspace view model with primary content, sidebar, and fly-out outlets:
/// </para>
/// <code><![CDATA[
/// public class WorkspaceViewModel : AbstractOutletContainer
/// {
///     public WorkspaceViewModel()
///     {
///         // Register outlets with their corresponding property names
///         Outlets.Add(OutletName.Primary, (nameof(MainContent), null));
///         Outlets.Add("sidebar", (nameof(SidebarContent), null));
///         Outlets.Add("flyout", (nameof(FlyoutContent), null));
///     }
///
///     // Properties that views bind to - automatically updated by base class
///     public object? MainContent => Outlets[OutletName.Primary].viewModel;
///     public object? SidebarContent => Outlets["sidebar"].viewModel;
///     public object? FlyoutContent => Outlets["flyout"].viewModel;
/// }
/// ]]></code>
/// <para><strong>Route Configuration</strong></para>
/// <code><![CDATA[
/// var routes = new Routes([
///     new Route
///     {
///         Path = "workspace",
///         ViewModelType = typeof(WorkspaceViewModel),
///         Children = new Routes([
///             // Default layout with editor and sidebar
///             new Route
///             {
///                 Path = "editor",
///                 ViewModelType = typeof(EditorViewModel)
///             },
///             new Route
///             {
///                 Path = "explorer",
///                 Outlet = "sidebar",
///                 ViewModelType = typeof(ExplorerViewModel)
///             },
///
///             // Alternate layout with documentation and properties
///             new Route
///             {
///                 Path = "docs",
///                 ViewModelType = typeof(DocumentationViewModel)
///             },
///             new Route
///             {
///                 Path = "properties",
///                 Outlet = "sidebar",
///                 ViewModelType = typeof(PropertiesViewModel)
///             },
///
///             // Optional flyout that can be added to any layout
///             new Route
///             {
///                 Path = "search",
///                 Outlet = "flyout",
///                 ViewModelType = typeof(SearchViewModel)
///             }
///         ])
///     }
/// ]);
/// ]]></code>
/// <para><strong>Navigation Examples:</strong></para>
/// <code><![CDATA[
/// // Basic layout with editor and explorer
/// router.Navigate("/workspace/editor//sidebar:explorer");
///
/// // Switch to documentation with properties
/// router.Navigate("/workspace/docs//sidebar:properties");
///
/// // Add search flyout to current layout
/// router.Navigate("/workspace/(docs//sidebar:properties//flyout:search)");
/// ]]></code>
/// </example>
public abstract class AbstractOutletContainer : ObservableObject, IOutletContainer, IDisposable
{
    /// <summary>
    /// Tracks whether this container has been disposed.
    /// </summary>
    private bool isDisposed;

    /// <summary>
    /// Gets the dictionary mapping outlet names to their property descriptors.
    /// </summary>
    /// <remarks>
    /// Each entry maps an outlet name to a tuple containing:
    /// - propertyName: The name of the property that exposes the outlet's content
    /// - viewModel: The current view model loaded in the outlet (may be null).
    /// </remarks>
    protected IDictionary<OutletName, (string propertyName, object? viewModel)> Outlets { get; }
        = new Dictionary<OutletName, (string propertyName, object? viewModel)>(OutletNameEqualityComparer.IgnoreCase);

    /// <inheritdoc />
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        outletName ??= OutletName.Primary;
        if (!this.Outlets.TryGetValue(outletName, out var outlet))
        {
            throw new ArgumentException($"unknown outlet name {outletName}", nameof(outletName));
        }

        // Proceed with the update only if we are not reusing the same view model.
        //  That way, we avoid unnecessary change notifications.
        if (outlet.viewModel == viewModel)
        {
            return;
        }

        // Dispose of the existing view model if it is IDisposable
        if (outlet.viewModel is IDisposable resource)
        {
            resource.Dispose();
        }

        this.OnPropertyChanging(outlet.propertyName);
        this.Outlets[outletName] = (outlet.propertyName, viewModel);
        this.OnPropertyChanged(outlet.propertyName);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc cref="IDisposable.Dispose" />
    /// <remarks>
    /// The outlets in the outlet container may have been activated, and as a
    /// result, they would have a view model which may be disposable. When the
    /// container is disposed of, we should dispose of all the activated outlet
    /// view models as well.
    /// </remarks>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            foreach (var entry in this.Outlets)
            {
                if (entry.Value.viewModel is IDisposable resource)
                {
                    resource.Dispose();
                }
            }
        }

        this.isDisposed = true;
    }
}
