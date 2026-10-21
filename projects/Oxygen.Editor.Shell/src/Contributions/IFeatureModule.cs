// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Scopes;

namespace Oxygen.Editor.Shell.Contributions;

/// <summary>
/// A feature's part in a workspace window: the panels and commands it contributes and the services, view models and
/// views it registers in each window's scope.
/// </summary>
/// <remarks>
/// The application registers one instance of each module. A workspace window's scope registers every module's services,
/// after its own, and then activates them, so a module's activation can use any module's services.
/// </remarks>
public interface IFeatureModule : IScopeModule<IWindowLayer>
{
    /// <summary>Gets the panels the feature contributes.</summary>
    public IReadOnlyList<PanelDescriptor> Panels { get; }

    /// <summary>Gets the commands the feature contributes; its parts run them.</summary>
    public IReadOnlyList<CommandDescriptor> Commands { get; }
}
