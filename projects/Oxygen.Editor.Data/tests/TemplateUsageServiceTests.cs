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
[TestCategory(nameof(TemplateUsageService))]
public partial class TemplateUsageServiceTests : DatabaseTests
{
    public TemplateUsageServiceTests()
    {
        this.Container.Register<TemplateUsageService>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task GetMostRecentlyUsedTemplatesAsync_ShouldReturnTemplatesInDescendingOrder()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<TemplateUsageService>();

            var templates = new List<TemplateUsage>
            {
                new() { Location = "Location1", LastUsedOn = DateTime.Now.AddDays(-1) },
                new() { Location = "Location2", LastUsedOn = DateTime.Now },
                new() { Location = "Location3", LastUsedOn = DateTime.Now.AddDays(-2) },
            };
            context.TemplatesUsageRecords.AddRange(templates);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            var result = await service.GetMostRecentlyUsedTemplatesAsync().ConfigureAwait(false);

            // Assert
            _ = result.Should().HaveCount(3);
            _ = result[0].Location.Should().Be("Location2");
            _ = result[1].Location.Should().Be("Location1");
            _ = result[2].Location.Should().Be("Location3");
        }
    }

    [TestMethod]
    public async Task GetTemplateUsageAsync_ShouldReturnCorrectTemplateUsage()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<TemplateUsageService>();

            var template = new TemplateUsage { Location = "Location1" };
            _ = context.TemplatesUsageRecords.Add(template);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            var result = await service.GetTemplateUsageAsync("Location1").ConfigureAwait(false);

            // Assert
            _ = result.Should().NotBeNull();
            _ = result!.Location.Should().Be("Location1");
            _ = result!.LastUsedOn.Should().BeBefore(DateTime.Now);
        }
    }

    [TestMethod]
    public async Task UpdateTemplateUsageAsync_ShouldIncrementTimesUsedIfExists()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<TemplateUsageService>();

            var template = new TemplateUsage { Location = "Location1", TimesUsed = 1 };
            _ = context.TemplatesUsageRecords.Add(template);
            _ = await context.SaveChangesAsync().ConfigureAwait(false);

            // Act
            await service.UpdateTemplateUsageAsync("Location1").ConfigureAwait(false);

            // Assert
            var result = await service.GetTemplateUsageAsync("Location1").ConfigureAwait(false);
            _ = result!.TimesUsed.Should().Be(2);
        }
    }

    [TestMethod]
    public async Task UpdateTemplateUsageAsync_ShouldAddNewRecordIfNotExists()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var service = scope.Resolve<TemplateUsageService>();

            // Act
            await service.UpdateTemplateUsageAsync("Location1").ConfigureAwait(false);

            // Assert
            var result = await service.GetTemplateUsageAsync("Location1").ConfigureAwait(false);
            _ = result.Should().NotBeNull();
            _ = result!.TimesUsed.Should().Be(1);
        }
    }

    [TestMethod]
    public async Task UpdateTemplateUsageAsync_ShouldThrowValidationExceptionForEmptyLocation()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var service = scope.Resolve<TemplateUsageService>();

            // Act
            var act = async () => await service.UpdateTemplateUsageAsync(string.Empty).ConfigureAwait(false);

            // Assert
            _ = await act.Should().ThrowAsync<ValidationException>()
                .WithMessage("The template location cannot be empty.").ConfigureAwait(false);
        }
    }
}
