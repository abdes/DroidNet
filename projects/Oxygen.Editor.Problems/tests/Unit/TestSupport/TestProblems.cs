// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Moq;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Assets.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Problems.Unit.Tests.TestSupport;

/// <summary>Builds the Problems panel's view model over fakes of what it reads.</summary>
internal static class TestProblems
{
    /// <summary>Creates the session of a project with no files.</summary>
    /// <returns>The session, which the caller disposes.</returns>
    internal static TestSession CreateSession()
        => new(TestProject.Create(new MockFileSystem()).Context with { Name = "Test", AuthoringMounts = [] });

    /// <summary>Creates the view model of a project with no files.</summary>
    /// <param name="session">The project's session, which the caller owns.</param>
    /// <param name="sources">The problem sources.</param>
    /// <param name="codeEditor">The code editor a problem's location opens in.</param>
    /// <param name="nodes">What shows the node a runtime problem names.</param>
    /// <param name="cookIssues">What shows the node a cook's problem names; one that accepts nothing by default.</param>
    /// <returns>The view model.</returns>
    internal static ProblemsViewModel CreateModel(TestSession session, IEnumerable<IAssetProblemSource> sources, ICodeEditorService codeEditor, IRuntimeProblemNodes nodes, ICookIssueReveal? cookIssues = null)
        => new(new AssetProblemSources(sources), codeEditor, session, nodes, cookIssues ?? Mock.Of<ICookIssueReveal>());
}
