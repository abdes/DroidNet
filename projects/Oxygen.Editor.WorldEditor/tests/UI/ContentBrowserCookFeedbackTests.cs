// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Tests;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks that current cooking work replaces stale browser feedback.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository configuration.")]
public sealed partial class ContentBrowserCookFeedbackTests : VisualUserInterfaceTests
{
    /// <summary>A pending cook clears the old banner; its final outcome stays in Cooking and releases the menu command.</summary>
    /// <returns>The asynchronous command regression.</returns>
    [TestMethod]
    public Task PendingCookClearsOldBannerAndCompletionDoesNotRestoreIt() => EnqueueAsync(async () =>
    {
        var pending = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.CookProjectAsync(It.IsAny<CancellationToken>())).Returns(pending.Task);
        var projects = new ProjectContextService();
        using var browser = new AssetsViewModel(
            Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.ICookRunService>(),
            new DroidNet.Mvvm.Converters.ViewModelToView(Mock.Of<DroidNet.Mvvm.IViewLocator>()),
            new ContentBrowserState(projects),
            projects,
            Mock.Of<IProjectManagerService>(),
            Mock.Of<IAuthoringTargetResolver>(),
            pipeline.Object,
            Mock.Of<Oxygen.Editor.ContentBrowser.AssetIdentity.IContentBrowserAssetProvider>(),
            Mock.Of<IOperationResultPublisher>(),
            Mock.Of<IStatusReducer>(),
            Mock.Of<DroidNet.Storage.IStorageProvider>(),
            new StrongReferenceMessenger(),
            Mock.Of<DroidNet.Aura.Dialogs.IDialogService>(),
            Mock.Of<DroidNet.Aura.Windowing.IWindowManagerService>())
        {
            IsOperationResultVisible = true,
            OperationResultTitle = "Cook Project",
            OperationResultMessage = "Previous cook failed.",
        };
        var work = browser.CookProjectCommand.ExecuteAsync(parameter: null);
        _ = browser.IsOperationResultVisible.Should().BeFalse();
        _ = browser.CookProjectCommand.CanExecute(parameter: null).Should().BeFalse();
        pending.SetResult(new(Guid.NewGuid(), CookTargetKind.Project, OperationStatus.Failed, [], [], Inspection: null, Validation: null));
        await work.ConfigureAwait(true);
        _ = browser.IsOperationResultVisible.Should().BeFalse();
        _ = browser.CookProjectCommand.CanExecute(parameter: null).Should().BeTrue();
    });
}
