// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.Workspace;

/// <summary>
///     The workspace window's own services: the panels and commands its features contribute.
/// </summary>
/// <remarks>
///     The window's scope composes this module with the Shell's window module and with its features
///     (<see cref="Compose" />), after its router and docking.
/// </remarks>
/// <param name="features">The features whose panels and commands the window shows.</param>
public sealed class WorkspaceWindowModule(IReadOnlyList<IFeatureModule> features) : IScopeModule<IWindowLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <summary>
    ///     Gets the modules of a workspace window's scope after its router and docking: this module, the Shell's and the
    ///     features'.
    /// </summary>
    /// <param name="features">The features whose panels and commands the window shows.</param>
    /// <returns>The modules, in registration and activation order.</returns>
    /// <remarks>The Shell's document service follows the window's tabs before the features activate.</remarks>
    public static IReadOnlyList<IScopeModule<IWindowLayer>> Compose(IReadOnlyList<IFeatureModule> features)
    {
        ArgumentNullException.ThrowIfNull(features);
        return
        [
            new WorkspaceWindowModule(features),
            new ShellWindowModule(),
            .. features,
        ];
    }

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        RegisterContributions(scope, features);
    }

    // The panels the features contribute, which the registry checks once.
    private static void RegisterContributions(IRegistrator scope, IReadOnlyList<IFeatureModule> modules)
        => scope.RegisterDelegate(
            _ => new ContributionRegistry(
                [.. modules.SelectMany(static module => module.Panels)],
                [.. modules.SelectMany(static module => module.Commands)]),
            Reuse.Singleton);
}
