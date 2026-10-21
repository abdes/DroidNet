// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;

namespace Oxygen.Editor.Shell.Contributions;

/// <summary>
/// A feature's part in a workspace window: the panels it contributes and the services, view models and views it
/// registers in each window.
/// </summary>
/// <remarks>
/// The application registers one instance of each module. A workspace window registers every module's services into
/// its container, after its own, and then activates them, so a module's activation can use any module's services.
/// </remarks>
public interface IFeatureModule
{
    /// <summary>Gets the panels the feature contributes.</summary>
    public IReadOnlyList<PanelDescriptor> Panels { get; }

    /// <summary>Registers the feature's services, view models and views in a workspace window.</summary>
    /// <param name="window">The window's container.</param>
    public void RegisterWindowServices(IContainer window);

    /// <summary>Creates the services that work from the window's start, before any of the feature's panels shows.</summary>
    /// <param name="window">The window's container, with every module's services registered.</param>
    public void ActivateWindowServices(IResolver window);
}
