// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     Maps a log entry's <see cref="OutputLogEntry.Source" /> to a short subsystem name, such as
///     <c>Engine</c> or <c>Cooker</c>, by the longest matching prefix. The application that sets up
///     the console supplies the map; the control has no built-in or default mapping.
/// </summary>
/// <remarks>
///     A prefix matches a source equal to it, or a source in which it is followed by a <c>.</c>: the
///     prefix <c>Oxygen.Editor</c> matches <c>Oxygen.Editor.Output.OutputView</c> but not
///     <c>Oxygen.EditorTools</c>. Comparisons are ordinal. The map is immutable and safe to use from
///     any thread.
/// </remarks>
public sealed class SubsystemMap
{
    private readonly KeyValuePair<string, string>[] byLongestPrefix;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SubsystemMap" /> class.
    /// </summary>
    /// <param name="entries">The <c>(prefix, subsystem)</c> pairs.</param>
    /// <exception cref="ArgumentException">
    ///     A prefix or a subsystem is empty, or a prefix appears more than once.
    /// </exception>
    public SubsystemMap(IEnumerable<KeyValuePair<string, string>> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);

        var copy = entries.ToArray();
        var prefixes = new HashSet<string>(StringComparer.Ordinal);
        foreach (var (prefix, subsystem) in copy)
        {
            if (string.IsNullOrEmpty(prefix))
            {
                throw new ArgumentException("A subsystem prefix must not be empty.", nameof(entries));
            }

            if (string.IsNullOrEmpty(subsystem))
            {
                throw new ArgumentException($"The subsystem for prefix '{prefix}' must not be empty.", nameof(entries));
            }

            if (!prefixes.Add(prefix))
            {
                throw new ArgumentException($"The subsystem prefix '{prefix}' appears more than once.", nameof(entries));
            }
        }

        this.Entries = copy;
        this.Names = [.. copy.Select(static entry => entry.Value).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal)];
        this.byLongestPrefix = [.. copy.OrderByDescending(static entry => entry.Key.Length)];
    }

    /// <summary>
    ///     Gets the <c>(prefix, subsystem)</c> pairs, in the order they were given.
    /// </summary>
    public IReadOnlyList<KeyValuePair<string, string>> Entries { get; }

    /// <summary>
    ///     Gets every subsystem the map can resolve to, once each, sorted ordinally: what a subsystem filter offers,
    ///     whether or not a subsystem has logged yet.
    /// </summary>
    public IReadOnlyList<string> Names { get; }

    /// <summary>
    ///     Resolves the subsystem of a source.
    /// </summary>
    /// <param name="source">The full producer name, such as a Serilog <c>SourceContext</c>.</param>
    /// <returns>
    ///     The subsystem of the longest matching prefix, or <see langword="null" /> when no prefix matches
    ///     or <paramref name="source" /> is <see langword="null" /> or empty.
    /// </returns>
    public string? Resolve(string? source)
    {
        if (string.IsNullOrEmpty(source))
        {
            return null;
        }

        foreach (var (prefix, subsystem) in this.byLongestPrefix)
        {
            if (source.StartsWith(prefix, StringComparison.Ordinal)
                && (source.Length == prefix.Length || source[prefix.Length] == '.'))
            {
                return subsystem;
            }
        }

        return null;
    }
}
