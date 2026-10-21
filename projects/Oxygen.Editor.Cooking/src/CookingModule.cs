// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

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
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.Register<ICookRecovery, CookRecovery>(Reuse.Singleton);
        window.Register<CookingPanelViewModel>(Reuse.Singleton);
        window.Register<CookingPanelView>(Reuse.Transient);
        window.Register<CookingPresenter>(Reuse.Singleton);
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window)
    {
        ArgumentNullException.ThrowIfNull(window);

        // The panel follows the cook runs, and answers explicit requests to come forward, before it is ever shown.
        _ = window.Resolve<CookingPresenter>();
    }
}
