// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// Records every <see cref="SceneAuthoring.Changed"/> event of one scene, with the thread that raised it.
/// </summary>
public sealed partial class ChangeRecorder : IDisposable
{
    private readonly Lock gate = new();
    private readonly List<RecordedChange> events = [];
    private readonly SceneAuthoring authoring;

    /// <summary>
    /// Initializes a new instance of the <see cref="ChangeRecorder"/> class and starts recording.
    /// </summary>
    /// <param name="authoring">The scene's authoring frame.</param>
    public ChangeRecorder(SceneAuthoring authoring)
    {
        ArgumentNullException.ThrowIfNull(authoring);
        this.authoring = authoring;
        authoring.Changed += this.OnChanged;
    }

    /// <summary>Gets the recorded events, in order.</summary>
    public IReadOnlyList<RecordedChange> All
    {
        get
        {
            lock (this.gate)
            {
                return [.. this.events];
            }
        }
    }

    /// <summary>Gets the number of recorded events.</summary>
    public int Count
    {
        get
        {
            lock (this.gate)
            {
                return this.events.Count;
            }
        }
    }

    /// <summary>
    /// Gets the changes of the only recorded event.
    /// </summary>
    /// <returns>The changes.</returns>
    /// <exception cref="InvalidOperationException">No event, or more than one, was recorded.</exception>
    public IReadOnlyList<SceneChange> Only()
    {
        var all = this.All;
        return all.Count == 1
            ? all[0].Changes
            : throw new InvalidOperationException($"Expected one Changed event, but {all.Count} were raised.");
    }

    /// <summary>Forgets the recorded events, typically after the arrange step.</summary>
    public void Clear()
    {
        lock (this.gate)
        {
            this.events.Clear();
        }
    }

    /// <inheritdoc/>
    public void Dispose() => this.authoring.Changed -= this.OnChanged;

    private void OnChanged(object? sender, SceneChangedEventArgs args)
    {
        lock (this.gate)
        {
            this.events.Add(new RecordedChange(args, Environment.CurrentManagedThreadId));
        }
    }
}
