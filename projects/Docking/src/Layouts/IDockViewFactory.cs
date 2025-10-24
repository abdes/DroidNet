// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Docking.Layouts;

/// <summary>
/// Defines a factory interface for creating views for docks.
/// </summary>
/// <remarks>
/// Implementing this interface allows you to create custom views for docks within a docking framework.
/// This is particularly useful for dynamically generating UI elements based on the state and type of the dock.
/// </remarks>
public interface IDockViewFactory
{
    /// <summary>
    /// Creates the <see cref="UIElement">UI representation</see> for a <see cref="IDock">dock</see>.
    /// </summary>
    /// <param name="dock">The dock object for which a <see cref="UIElement"/> needs to be created.</param>
    /// <returns>
    /// A <see cref="UIElement"/> for the dock, which can be placed into the docking workspace visual tree.
    /// </returns>
    /// <remarks>
    /// <para>
    /// This method is responsible for generating the visual representation of a dock. The returned <see cref="UIElement"/>
    /// should be fully configured and ready to be added to the visual tree.
    /// </para>
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// public class CustomDockViewFactory : IDockViewFactory
    /// {
    ///     public UIElement CreateViewForDock(IDock dock)
    ///     {
    ///         // Create a new UIElement based on the dock's properties
    ///         var dockView = new DockView
    ///         {
    ///             DataContext = dock
    ///         };
    ///         return dockView;
    ///     }
    /// }
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Guidelines:</strong>
    /// <list type="bullet">
    /// <item>Ensure that the created <see cref="UIElement"/> is properly configured and bound to the dock's properties.</item>
    /// <item>Handle any exceptions that might occur during the creation of the view to avoid runtime errors.</item>
    /// <item>Consider caching views if the creation process is expensive.</item>
    /// </list>
    /// </para>
    /// </remarks>
    public UIElement CreateViewForDock(IDock dock);
}
