// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Problems;

/// <summary>The Problems panel: every asset family's problems and the running scene's.</summary>
public sealed class ProblemsModule : IFeatureModule
{
    /// <summary>The Problems panel's identity.</summary>
    public const string PanelId = "problems";

    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, ProblemsViewModel.BaseTitle, typeof(ProblemsViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.Register<IRuntimeProblemNodes, RuntimeProblemNodes>(Reuse.Singleton);
        window.Register<ProblemsViewModel>(Reuse.Singleton);
        window.Register<ProblemsView>(Reuse.Transient);
        window.Register<ProblemsPresenter>(Reuse.Singleton);
        window.RegisterMapping<IProblemReveal, ProblemsPresenter>();
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window)
    {
        ArgumentNullException.ThrowIfNull(window);

        // The problems are collected, and the panel follows them, before it is ever shown.
        _ = window.Resolve<ProblemsPresenter>();
    }
}
