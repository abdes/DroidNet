// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Managed.Assets.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Problems.Unit.Tests.TestSupport;

/// <summary>Builds the Problems panel's view model over fakes of what it reads.</summary>
internal static class TestProblems
{
    /// <summary>Creates the view model of a project with no files.</summary>
    /// <param name="sources">The problem sources.</param>
    /// <param name="codeEditor">The code editor a problem's location opens in.</param>
    /// <param name="nodes">What shows the node a runtime problem names.</param>
    /// <returns>The view model.</returns>
    internal static ProblemsViewModel CreateModel(IEnumerable<IAssetProblemSource> sources, ICodeEditorService codeEditor, IRuntimeProblemNodes nodes)
    {
        var projects = new Mock<IProjectContextService>();
        _ = projects.SetupGet(static service => service.ActiveProject).Returns(TestProject.Create(new MockFileSystem()).Context with { ProjectId = Guid.Empty, Name = "Test", AuthoringMounts = [] });
        return new ProblemsViewModel(sources, codeEditor, projects.Object, nodes);
    }
}
