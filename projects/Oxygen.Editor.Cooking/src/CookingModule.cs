// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Cooking;

/// <summary>The Cooking panel: the project's cook runs with their issues, output and assets.</summary>
public sealed class CookingModule : IFeatureModule
{
    /// <summary>The Cooking panel's identity.</summary>
    public const string PanelId = "cook";

    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, "Cooking", typeof(CookingPanelViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    /// <remarks>
    /// The panel follows the cook runs, and answers explicit requests to come forward, before it is ever shown.
    /// </remarks>
    public IReadOnlyList<Type> Activation { get; } = [typeof(CookingPresenter)];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.RegisterMany([typeof(ICookRecovery), typeof(ICookIssueReveal)], typeof(CookRecovery), Reuse.Singleton);
        scope.Register<CookingPanelViewModel>(Reuse.Singleton);
        scope.Register<CookingPanelView>(Reuse.Transient);
        scope.Register<CookingPresenter>(Reuse.Singleton);
    }
}
