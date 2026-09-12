// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks automatic Save scheduling, coalescing, and blocked explicit cook recovery.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AutomaticCookServiceTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Repeated saves retain only the latest follow-up while the current cook owns its inputs.</summary>
    /// <returns>The asynchronous scheduling regression.</returns>
    [TestMethod]
    public async Task ChangedSavesCoalesceBehindActiveWork()
    {
        var projects = CreateProjects();
        var pipeline = new Mock<IContentPipelineService>();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var followUp = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFollowUp = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var calls = 0;
        _ = pipeline.Setup(value => value.CookSavedAssetAsync(It.IsAny<Uri>(), projects.ActiveProject!, It.IsAny<CancellationToken>()))
            .Returns(() =>
            {
                if (Interlocked.Increment(ref calls) == 1)
                {
                    entered.SetResult();
                    return release.Task;
                }

                followUp.SetResult();
                return releaseFollowUp.Task;
            });
        using var service = new AutomaticCookService(projects, pipeline.Object, Mock.Of<ICookRunService>(value => value.Runs == Array.Empty<CookRunSnapshot>()), NullLogger<AutomaticCookService>.Instance);
        var path = Path.Combine(projects.ActiveProject!.ProjectRoot, "Content", "Scenes", "Main.oscene.json");
        service.NotifySaved(path, "first");
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        service.NotifySaved(path, "second");
        service.NotifySaved(path, "third");
        service.NotifySaved(path, "third");
        _ = calls.Should().Be(1);
        release.SetResult(Succeeded());
        await followUp.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = calls.Should().Be(2);
        pipeline.Verify(value => value.CookSavedAssetAsync(new Uri("asset:///Content/Scenes/Main.oscene.json"), projects.ActiveProject!, It.IsAny<CancellationToken>()), Times.Exactly(2));
        releaseFollowUp.SetResult(Succeeded());
    }

    /// <summary>A source Save resumes an existing project cook instead of submitting duplicate asset work.</summary>
    [TestMethod]
    public void SaveResumesTheCookWaitingForThatDocument()
    {
        var projects = CreateProjects();
        var project = projects.ActiveProject!;
        var path = Path.Combine(project.ProjectRoot, "Content", "Scenes", "Main.oscene.json");
        var operationId = Guid.NewGuid();
        var runs = new Mock<ICookRunService>();
        _ = runs.SetupGet(value => value.Runs).Returns(
        [
            new CookRunSnapshot
            {
                OperationId = operationId, ProjectId = project.ProjectId, ProjectRoot = project.ProjectRoot,
                DisplayName = "Project", Request = new(CookTargetKind.Project, ScopeUri: null), State = CookRunState.NeedsSave,
                UnsavedDocuments = [new(Guid.NewGuid(), path, "Main", 1, 0, true, "old")],
            },
        ]);
        _ = runs.Setup(value => value.ResumeAfterSave(operationId)).Returns(value: true);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        using var service = new AutomaticCookService(projects, pipeline.Object, runs.Object, NullLogger<AutomaticCookService>.Instance);
        service.NotifySaved(path, "saved");
        runs.Verify(value => value.ResumeAfterSave(operationId), Times.Once);
        pipeline.VerifyNoOtherCalls();
    }

    /// <summary>Unchanged saves and sources outside the active project do not submit automatic cooks.</summary>
    [TestMethod]
    public void UnchangedAndForeignSourcesDoNotQueue()
    {
        var projects = CreateProjects();
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        using var service = new AutomaticCookService(projects, pipeline.Object, Mock.Of<ICookRunService>(value => value.Runs == Array.Empty<CookRunSnapshot>()), NullLogger<AutomaticCookService>.Instance);
        service.NotifySaved(Path.Combine(projects.ActiveProject!.ProjectRoot, "Content", "Main.oscene.json"), "unchanged", contentChanged: false);
        service.NotifySaved(Path.Combine(Path.GetTempPath(), "Foreign", "Content", "Main.oscene.json"), "foreign");
        pipeline.VerifyNoOtherCalls();
    }

    private static ContentCookResult Succeeded() => new(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);

    private static ProjectContextService CreateProjects()
    {
        var projects = new ProjectContextService();
        projects.Activate(new ProjectContext
        {
            ProjectId = Guid.NewGuid(), ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            Name = "Save project", Category = Category.Games, AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
        });
        return projects;
    }
}
