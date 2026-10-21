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
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.Register<OutputViewModel>(Reuse.Singleton);
        window.Register<OutputView>(Reuse.Transient);
        window.Register<PlayerLogOutput>(Reuse.Singleton);
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window)
    {
        ArgumentNullException.ThrowIfNull(window);

        // The player's log reaches the buffer whether or not the panel shows.
        _ = window.Resolve<PlayerLogOutput>();
    }
}
