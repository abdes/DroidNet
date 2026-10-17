// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// An edit whose every answer is set by the test, for testing the authoring frame itself. Each call is recorded in
/// <see cref="Log"/>, which its revert shares.
/// </summary>
/// <remarks>
/// By default the probe validates, applies with no effect on the scene, and reports one
/// <see cref="SceneChange.LayoutChanged"/>, so the frame records it. Give it an <see cref="Effect"/> and a
/// <see cref="RevertEffect"/> to change the document for real.
/// </remarks>
public sealed record ProbeEdit : ISceneEdit
{
    /// <summary>Gets the edit's label; the revert's label ends in <c>(revert)</c>.</summary>
    public string Label { get; init; } = "Probe";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets { get; init; } = [];

    /// <inheritdoc/>
    public bool ChangesDocument { get; init; } = true;

    /// <summary>Gets what <see cref="ISceneEdit.Validate"/> returns.</summary>
    public SceneEditValidation Validation { get; init; } = SceneEditValidation.Valid;

    /// <summary>Gets the validation that <see cref="ISceneEdit.Apply"/> rejects with; the probe applies when <see langword="null"/>.</summary>
    public SceneEditValidation? Rejection { get; init; }

    /// <summary>Gets the exception that <see cref="ISceneEdit.Apply"/> throws, if any.</summary>
    public Exception? Failure { get; init; }

    /// <summary>Gets the changes the probe reports when it applies; its revert reports the same.</summary>
    public IReadOnlyList<SceneChange> Changes { get; init; } = [new SceneChange.LayoutChanged()];

    /// <summary>
    /// Gets what applying does, before the probe applies or rejects: change the scene, or warn through the scope.
    /// </summary>
    public Action<SceneEditScope>? Effect { get; init; }

    /// <summary>Gets what applying the revert does.</summary>
    public Action<SceneEditScope>? RevertEffect { get; init; }

    /// <summary>Gets the record of the probe's calls, and of its reverts'.</summary>
    public ProbeLog Log { get; init; } = new();

    /// <summary>
    /// Makes a probe that renames a node, and whose revert restores the name.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <param name="from">The current name.</param>
    /// <param name="to">The new name.</param>
    /// <returns>The probe.</returns>
    public static ProbeEdit Renaming(NodeId node, string from, string to)
        => new()
        {
            Label = $"Rename {from}",
            Targets = [node],
            Changes = [new SceneChange.NodeRenamed(node)],
            Effect = scope => scope.FindNode(node)!.Name = to,
            RevertEffect = scope => scope.FindNode(node)!.Name = from,
        };

    /// <inheritdoc/>
    public SceneEditValidation Validate(SceneEditScope scope)
    {
        this.Log.Add(this.Label, ProbeStep.Validate);
        return this.Validation;
    }

    /// <inheritdoc/>
    public SceneEditApplication Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        this.Log.Add(this.Label, ProbeStep.Apply);
        if (this.Failure is { } failure)
        {
            throw failure;
        }

        this.Effect?.Invoke(scope);
        if (this.Rejection is { } rejection)
        {
            return new SceneEditApplication.Rejected(rejection);
        }

        var revert = this with
        {
            Label = this.Label.EndsWith(" (revert)", StringComparison.Ordinal) ? this.Label[..^" (revert)".Length] : this.Label + " (revert)",
            Effect = this.RevertEffect,
            RevertEffect = this.Effect,
            Validation = SceneEditValidation.Valid,
            Rejection = null,
            Failure = null,
        };
        return new SceneEditApplication.Applied(revert, this.Changes);
    }
}
