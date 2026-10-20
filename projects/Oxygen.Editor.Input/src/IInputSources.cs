// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// The saved input sources of the active project, as their files are now, and their problems as
/// <see cref="InputProblem"/>.
/// </summary>
/// <remarks>
/// Every read is of an immutable snapshot that each change replaces at once. Events are raised on the
/// <see cref="SynchronizationContext"/> that was current when the handler subscribed, or on the thread pool when there
/// was none.
/// </remarks>
public interface IInputSources : IAssetProblemSource
{
    /// <summary>Occurs when an input source was added, changed or removed, or when the project changed.</summary>
    public event EventHandler<InputSourceChangedEventArgs>? Changed;

    /// <summary>Gets every input source of the project that matches the engine's input schema.</summary>
    public IReadOnlyCollection<InputContainer> All { get; }

    /// <summary>Brings the facts of some source files up to date with the files.</summary>
    /// <remarks>A source whose file changed since it was read, such as one whose change is still being read, is read
    /// now; files that are not input sources of the open project are ignored.</remarks>
    /// <param name="sources">The source files, as the catalog names them, such as <c>/Content/Input/Player.input.json</c>.</param>
    public void Check(IReadOnlyCollection<AssetPath> sources);

    /// <summary>Finds an input source.</summary>
    /// <param name="container">The source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <returns>The source, or <see langword="null"/> when the project has no such source or it does not match the
    /// engine's input schema.</returns>
    public InputContainer? Find(AssetPath container);
}
