// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input.Testing;

/// <summary>
/// An in-memory <see cref="IInputSources"/> for tests: the input sources a test adds, read by the same format reader
/// as <see cref="InputFacts"/>.
/// </summary>
/// <remarks>Events are raised at once, on the thread that changed the sources.</remarks>
public sealed class FakeInputSources : IInputSources
{
    private readonly Dictionary<AssetPath, InputContainer> containers = [];

    /// <inheritdoc />
    public event EventHandler<InputSourceChangedEventArgs>? Changed;

    /// <inheritdoc />
    public event EventHandler? ProblemsChanged;

    /// <inheritdoc />
    public IReadOnlyCollection<InputContainer> All => [.. this.containers.Values.OrderBy(static container => container.Identity.VirtualPath, StringComparer.Ordinal)];

    /// <inheritdoc />
    public IReadOnlyList<AssetProblem> Problems { get; private set; } = [];

    /// <inheritdoc />
    public InputContainer? Find(AssetPath container)
    {
        ArgumentNullException.ThrowIfNull(container);
        return this.containers.GetValueOrDefault(container);
    }

    /// <summary>Adds or replaces an input source.</summary>
    /// <param name="container">The source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <param name="json">The source's JSON, which must match the engine's input schema.</param>
    /// <returns>The source.</returns>
    public InputContainer Add(string container, string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        var source = new InputContainer(AssetPath.Parse(container), InputSourceReader.Read(Encoding.UTF8.GetBytes(json)));
        this.containers[source.Identity] = source;
        this.Changed?.Invoke(this, new InputSourceChangedEventArgs(source.Identity));
        return source;
    }

    /// <summary>Removes an input source.</summary>
    /// <param name="container">The source's identity.</param>
    public void Remove(string container)
    {
        var identity = AssetPath.Parse(container);
        if (this.containers.Remove(identity))
        {
            this.Changed?.Invoke(this, new InputSourceChangedEventArgs(identity));
        }
    }

    /// <summary>Replaces the problems.</summary>
    /// <param name="problems">The problems.</param>
    public void SetProblems(IReadOnlyList<AssetProblem> problems)
    {
        ArgumentNullException.ThrowIfNull(problems);
        this.Problems = problems;
        this.ProblemsChanged?.Invoke(this, EventArgs.Empty);
    }
}
