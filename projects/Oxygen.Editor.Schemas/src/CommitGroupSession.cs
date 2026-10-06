// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Handle to a single in-flight commit-group session.
/// </summary>
public sealed class CommitGroupSession
{
    /// <summary>
    /// Initializes a new instance of the <see cref="CommitGroupSession"/> class.
    /// </summary>
    /// <param name="key">The session key.</param>
    /// <param name="nodes">The captured node set.</param>
    /// <param name="before">The pre-edit snapshot.</param>
    /// <param name="label">The history label.</param>
    public CommitGroupSession(string key, IReadOnlyList<Guid> nodes, PropertySnapshot before, string label)
    {
        this.Key = key;
        this.Nodes = nodes;
        this.Before = before;
        this.Label = label;
    }

    /// <summary>
    /// Gets the session key.
    /// </summary>
    public string Key { get; }

    /// <summary>
    /// Gets the captured node set.
    /// </summary>
    public IReadOnlyList<Guid> Nodes { get; }

    /// <summary>
    /// Gets the pre-edit snapshot.
    /// </summary>
    public PropertySnapshot Before { get; }

    /// <summary>
    /// Gets the history label.
    /// </summary>
    public string Label { get; }

    /// <summary>
    /// Gets or sets the most recent preview snapshot. The
    /// <see cref="CommitGroupController.Close"/> method assigns
    /// <see cref="After"/> from this value at commit time.
    /// </summary>
    public PropertySnapshot? LastPreview { get; set; }

    /// <summary>
    /// Gets the post-edit snapshot. Set by <see cref="CommitGroupController.Close"/>.
    /// </summary>
    public PropertySnapshot? After { get; internal set; }
}
