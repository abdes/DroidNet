// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Problems.Unit.Tests.TestSupport;

/// <summary>The running scene's problems, which a test reports and clears, and the node selection that records what was asked.</summary>
internal sealed class FakeRuntimeProblems : IRuntimeProblemSource, IRuntimeProblemNodes
{
    private readonly List<RuntimeScriptProblem> problems = [];

    public event EventHandler? ProblemsChanged;

    public IReadOnlyList<AssetProblem> Problems => this.problems;

    public List<RuntimeScriptProblem> Selected { get; } = [];

    public DocumentId Document { get; } = DocumentId.New();

    public NodeId Node { get; } = NodeId.New();

    // A problem that names a node is found in the scene that played.
    public (DocumentId Document, NodeId Node)? NodeOf(RuntimeScriptProblem problem)
        => problem.SlotNode is not null ? (this.Document, this.Node) : null;

    public Task<bool> SelectAsync(RuntimeScriptProblem problem)
    {
        this.Selected.Add(problem);
        return Task.FromResult(true);
    }

    public void Report(RuntimeScriptProblem problem)
    {
        this.problems.Add(problem);
        this.ProblemsChanged?.Invoke(this, EventArgs.Empty);
    }

    public void Clear()
    {
        this.problems.Clear();
        this.ProblemsChanged?.Invoke(this, EventArgs.Empty);
    }
}
