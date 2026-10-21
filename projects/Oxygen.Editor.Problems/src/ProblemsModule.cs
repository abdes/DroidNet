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
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    /// <remarks>
    /// The problems are collected, and the panel follows them, before it is ever shown.
    /// </remarks>
    public IReadOnlyList<Type> Activation { get; } = [typeof(ProblemsPresenter)];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IRuntimeProblemNodes, RuntimeProblemNodes>(Reuse.Singleton);
        scope.Register<ProblemsViewModel>(Reuse.Singleton);
        scope.Register<ProblemsView>(Reuse.Transient);
        scope.Register<ProblemsPresenter>(Reuse.Singleton);
        scope.RegisterMapping<IProblemReveal, ProblemsPresenter>();
    }
}
