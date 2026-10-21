// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.Output;

/// <summary>The Output panel: the editor's log, and the player's from the window's start.</summary>
public sealed class OutputModule : IFeatureModule
{
    /// <summary>The Output panel's identity.</summary>
    public const string PanelId = "log";

    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, "Output", typeof(OutputViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    /// <remarks>
    /// The player's log reaches the buffer whether or not the panel shows.
    /// </remarks>
    public IReadOnlyList<Type> Activation { get; } = [typeof(PlayerLogOutput)];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<OutputViewModel>(Reuse.Singleton);
        scope.Register<OutputView>(Reuse.Transient);
        scope.Register<PlayerLogOutput>(Reuse.Singleton);
    }
}
