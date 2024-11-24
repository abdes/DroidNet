// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using DryIoc;
using FluentAssertions;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ProjectUsageService))]
public partial class ProjectUsageServiceTests : DatabaseTests
{
    public ProjectUsageServiceTests()
    {
        this.Container.Register<ProjectUsageService>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task GetMostRecentlyUsedProjectsAsync_ShouldReturnProjectsInDescendingOrder()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var projects = new List<ProjectUsage>
            {
                new() { Name = "Project1", Location = "Location1", LastUsedOn = DateTime.Now.AddDays(-1) },
                new() { Name = "Project2", Location = "Location2", LastUsedOn = DateTime.Now },
                new() { Name = "Project3", Location = "Location3", LastUsedOn = DateTime.Now.AddDays(-2) },
            };
            context.ProjectUsageRecords.AddRange(projects);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            var result = await service.GetMostRecentlyUsedProjectsAsync().ConfigureAwait(false);

            // Assert
            _ = result.Should().HaveCount(3);
            _ = result[0].Name.Should().Be("Project2");
            _ = result[1].Name.Should().Be("Project1");
            _ = result[2].Name.Should().Be("Project3");
        }
    }

    [TestMethod]
    public async Task GetProjectUsageAsync_ShouldReturnCorrectProjectUsage()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var project = new ProjectUsage { Name = "Project1", Location = "Location1" };
            _ = context.ProjectUsageRecords.Add(project);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            var result = await service.GetProjectUsageAsync("Project1", "Location1").ConfigureAwait(false);

            // Assert
            _ = result.Should().NotBeNull();
            _ = result!.Name.Should().Be("Project1");
            _ = result.Location.Should().Be("Location1");
            _ = result!.LastUsedOn.Should().BeBefore(DateTime.Now);
        }
    }

    [TestMethod]
    public async Task UpdateProjectUsageAsync_ShouldIncrementTimesOpenedIfExists()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var project = new ProjectUsage { Name = "Project1", Location = "Location1", TimesOpened = 1 };
            _ = context.ProjectUsageRecords.Add(project);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            await service.UpdateProjectUsageAsync("Project1", "Location1").ConfigureAwait(false);

            // Assert
            var result = await service.GetProjectUsageAsync("Project1", "Location1").ConfigureAwait(false);
            _ = result!.TimesOpened.Should().Be(2);
        }
    }

    [TestMethod]
    public async Task UpdateProjectUsageAsync_ShouldAddNewRecordIfNotExists()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var service = scope.Resolve<ProjectUsageService>();

            // Act
            await service.UpdateProjectUsageAsync("Project1", "Location1").ConfigureAwait(false);

            // Assert
            var result = await service.GetProjectUsageAsync("Project1", "Location1").ConfigureAwait(false);
            _ = result.Should().NotBeNull();
            _ = result!.TimesOpened.Should().Be(1);
        }
    }

    [TestMethod]
    public async Task UpdateContentBrowserStateAsync_ShouldUpdateContentBrowserState()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var project = new ProjectUsage
            {
                Name = "Project1",
                Location = "Location1",
                ContentBrowserState = "OldState",
            };
            _ = context.ProjectUsageRecords.Add(project);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            await service.UpdateContentBrowserStateAsync("Project1", "Location1", "NewState").ConfigureAwait(false);

            // Assert
            var result = await service.GetProjectUsageAsync("Project1", "Location1").ConfigureAwait(false);
            _ = result!.ContentBrowserState.Should().Be("NewState");
        }
    }

    [TestMethod]
    public async Task UpdateContentBrowserStateAsync_ShouldFireDebugAssertionIfRecordDoesNotExist()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var service = scope.Resolve<ProjectUsageService>();

            // Act
            await service.UpdateContentBrowserStateAsync("NonExistentProject", "NonExistentLocation", "NewState").ConfigureAwait(false);

            // Assert
#if DEBUG
            _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.Contains("Project usage record must exist before updating content browser state"));
#else
            _ = this.TraceListener.RecordedMessages.Should().BeEmpty();
#endif
        }
    }

    [TestMethod]
    public async Task UpdateLastOpenedSceneAsync_ShouldUpdateLastOpenedScene()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var project = new ProjectUsage
            {
                Name = "Project1",
                Location = "Location1",
                LastOpenedScene = "OldScene",
            };
            _ = context.ProjectUsageRecords.Add(project);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            await service.UpdateLastOpenedSceneAsync("Project1", "Location1", "NewScene").ConfigureAwait(false);

            // Assert
            var result = await service.GetProjectUsageAsync("Project1", "Location1").ConfigureAwait(false);
            _ = result!.LastOpenedScene.Should().Be("NewScene");
        }
    }

    [TestMethod]
    public async Task UpdateLastOpenedSceneAsync_ShouldFireDebugAssertionIfRecordDoesNotExist()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var service = scope.Resolve<ProjectUsageService>();

            // Act
            await service.UpdateLastOpenedSceneAsync("NonExistentProject", "NonExistentLocation", "NewScene").ConfigureAwait(false);

            // Assert
#if DEBUG
            _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.Contains("Project usage record must exist before updating last opened scene"));
#else
            _ = this.TraceListener.RecordedMessages.Should().BeEmpty();
#endif
        }
    }

    [TestMethod]
    public async Task UpdateProjectNameAndLocationAsync_ShouldUpdateNameAndLocation()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var project = new ProjectUsage { Name = "OldName", Location = "OldLocation" };
            _ = context.ProjectUsageRecords.Add(project);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            await service.UpdateProjectNameAndLocationAsync("OldName", "OldLocation", "NewName", "NewLocation").ConfigureAwait(false);

            // Assert
            var result = await service.GetProjectUsageAsync("NewName", "NewLocation").ConfigureAwait(false);
            _ = result.Should().NotBeNull();
            _ = result!.Name.Should().Be("NewName");
            _ = result.Location.Should().Be("NewLocation");
        }
    }

    [TestMethod]
    public async Task UpdateProjectNameAndLocationAsync_ShouldThrowValidationExceptionForEmptyNewName()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var project = new ProjectUsage { Name = "OldName", Location = "OldLocation" };
            _ = context.ProjectUsageRecords.Add(project);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            var act = async () => await service.UpdateProjectNameAndLocationAsync("OldName", "OldLocation", string.Empty).ConfigureAwait(false);

            // Assert
            _ = await act.Should().ThrowAsync<ValidationException>().WithMessage("*project name cannot be empty*").ConfigureAwait(false);
        }
    }

    [TestMethod]
    public async Task UpdateProjectNameAndLocationAsync_ShouldThrowValidationExceptionForEmptyNewLocation()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<ProjectUsageService>();

            var project = new ProjectUsage { Name = "OldName", Location = "OldLocation" };
            _ = context.ProjectUsageRecords.Add(project);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            var act = async () => await service.UpdateProjectNameAndLocationAsync("OldName", "OldLocation", "NewName", string.Empty).ConfigureAwait(false);

            // Assert
            _ = await act.Should().ThrowAsync<ValidationException>().WithMessage("*project location cannot be empty*").ConfigureAwait(false);
        }
    }
}
