// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.Shell.Contributions;

/// <summary>
/// The panels and commands that features contribute, checked once: two contributions with one identity fail.
/// </summary>
public sealed class ContributionRegistry
{
    private readonly Dictionary<string, PanelDescriptor> panels;
    private readonly Dictionary<string, CommandDescriptor> commands;

    /// <summary>Initializes a new instance of the <see cref="ContributionRegistry"/> class.</summary>
    /// <param name="panels">The contributed panels.</param>
    /// <param name="commands">The contributed commands, beside the Shell's own.</param>
    /// <exception cref="InvalidOperationException">Two panels, or two commands, have the same identity.</exception>
    public ContributionRegistry(IEnumerable<PanelDescriptor> panels, IEnumerable<CommandDescriptor> commands)
    {
        ArgumentNullException.ThrowIfNull(panels);
        ArgumentNullException.ThrowIfNull(commands);
        this.panels = Index(panels, static panel => panel.Id, "panel");
        this.commands = Index(ShellCommands.All.Concat(commands), static command => command.Id, "command");
    }

    /// <summary>Gets the contributed panels.</summary>
    public IReadOnlyCollection<PanelDescriptor> Panels => this.panels.Values;

    /// <summary>Gets the Shell's commands and the contributed ones.</summary>
    public IReadOnlyCollection<CommandDescriptor> Commands => this.commands.Values;

    /// <summary>Finds a contributed panel.</summary>
    /// <param name="id">The panel's identity.</param>
    /// <returns>The panel, or <see langword="null"/> when no feature contributes it.</returns>
    public PanelDescriptor? FindPanel(string id) => this.panels.GetValueOrDefault(id);

    private static Dictionary<string, T> Index<T>(IEnumerable<T> items, Func<T, string> idOf, string what)
    {
        var index = new Dictionary<string, T>(StringComparer.Ordinal);
        foreach (var item in items)
        {
            if (!index.TryAdd(idOf(item), item))
            {
                throw new InvalidOperationException($"Two {what}s have the identity '{idOf(item)}'.");
            }
        }

        return index;
    }
}
