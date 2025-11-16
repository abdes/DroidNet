// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Caching.Memory;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Services;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(TemplateUsageService))]
public class TemplateUsageServiceTests : DatabaseTests
{
    public TemplateUsageServiceTests()
    {
        // Register a factory function that creates a NEW PersistentState instance each time
        // using the registered DbContextOptions, not the scoped PersistentState
        this.Container.RegisterDelegate<Func<PersistentState>>(
            resolver =>
            {
                var options = resolver.Resolve<DbContextOptions<PersistentState>>();
                return () => new PersistentState(options);
            },
            Reuse.Scoped);

        this.Container.Register<TemplateUsageService>(Reuse.Scoped);
#pragma warning disable CA2000 // Dispose objects before losing scope
        // DryIoc will properly dispose of this instance when the container is disposed
        this.Container.RegisterInstance<IMemoryCache>(new MemoryCache(new MemoryCacheOptions()));
#pragma warning restore CA2000 // Dispose objects before losing scope
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
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

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
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

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
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

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

    [TestMethod]
    public async Task HasRecentlyUsedTemplatesAsync_ShouldReturnTrueIfTemplatesExist()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<TemplateUsageService>();

            var template = new TemplateUsage { Location = "Location1", LastUsedOn = DateTime.Now };
            _ = context.TemplatesUsageRecords.Add(template);
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

            // Act
            var result = await service.HasRecentlyUsedTemplatesAsync().ConfigureAwait(false);

            // Assert
            _ = result.Should().BeTrue();
        }
    }

    [TestMethod]
    public async Task HasRecentlyUsedTemplatesAsync_ShouldReturnFalseIfNoTemplatesExist()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var service = scope.Resolve<TemplateUsageService>();

            // Act
            var result = await service.HasRecentlyUsedTemplatesAsync().ConfigureAwait(false);

            // Assert
            _ = result.Should().BeFalse();
        }
    }

    [TestMethod]
    public async Task DeleteTemplateUsageAsync_ShouldRemoveTemplateUsage()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<TemplateUsageService>();

            var template = new TemplateUsage { Location = "Location1" };
            _ = context.TemplatesUsageRecords.Add(template);
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

            // Act
            await service.DeleteTemplateUsageAsync("Location1").ConfigureAwait(false);

            // Assert
            var result = await service.GetTemplateUsageAsync("Location1").ConfigureAwait(false);
            _ = result.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task GetTemplateUsageAsync_ShouldReturnFromCacheIfAvailable()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var service = scope.Resolve<TemplateUsageService>();
            var memoryCache = scope.Resolve<IMemoryCache>();

            var template = new TemplateUsage { Location = "Location1", TimesUsed = 1 };
            _ = memoryCache.Set($"{TemplateUsageService.CacheKeyPrefix}Location1", template);

            // Act
            var result = await service.GetTemplateUsageAsync("Location1").ConfigureAwait(false);

            // Assert
            _ = result.Should().NotBeNull();
            _ = result!.Location.Should().Be("Location1");
            _ = result!.TimesUsed.Should().Be(1);
        }
    }

    [TestMethod]
    public async Task UpdateTemplateUsageAsync_ShouldUpdateCache()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            // Arrange
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<TemplateUsageService>();
            var memoryCache = scope.Resolve<IMemoryCache>();

            // Seed the database with an existing template
            var template = new TemplateUsage { Location = "Location1", TimesUsed = 1 };
            _ = context.TemplatesUsageRecords.Add(template);
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

            // Also set the cache to verify it gets updated
            _ = memoryCache.Set($"{TemplateUsageService.CacheKeyPrefix}Location1", template);

            // Act
            await service.UpdateTemplateUsageAsync("Location1").ConfigureAwait(false);

            // Assert
            var result = memoryCache.Get<TemplateUsage>($"{TemplateUsageService.CacheKeyPrefix}Location1");
            _ = result.Should().NotBeNull();
            _ = result!.TimesUsed.Should().Be(2);
        }
    }

    [TestMethod]
    public async Task UpdateTemplateUsageAsync_ConcurrentIncrements_ResultsInCorrectCount()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var context = scope.Resolve<PersistentState>();
            var service = scope.Resolve<TemplateUsageService>();

            var template = new TemplateUsage { Location = "LocationConcurrent" };
            _ = context.TemplatesUsageRecords.Add(template);
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

            var tasks = Enumerable.Range(0, 5).Select(_ => service.UpdateTemplateUsageAsync("LocationConcurrent"));
            await Task.WhenAll(tasks).ConfigureAwait(false);

            var result = await service.GetTemplateUsageAsync("LocationConcurrent").ConfigureAwait(false);
            _ = result!.TimesUsed.Should().Be(5);
        }
    }

    [TestMethod]
    public async Task GetTemplateUsageAsync_ReturnedObjectIsClone_CacheNotModifiedByConsumer()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var service = scope.Resolve<TemplateUsageService>();
            var memoryCache = scope.Resolve<IMemoryCache>();

            var template = new TemplateUsage { Location = "Location1", TimesUsed = 1 };
            _ = memoryCache.Set(TemplateUsageService.CacheKeyPrefix + "Location1", template);

            var obj = await service.GetTemplateUsageAsync("Location1").ConfigureAwait(false);
            obj!.TimesUsed = 999; // mutate the returned object

            var obj2 = await service.GetTemplateUsageAsync("Location1").ConfigureAwait(false);
            _ = obj2!.TimesUsed.Should().Be(1);
        }
    }

    [TestMethod]
    public async Task DeleteTemplateUsageAsync_RemovesCacheEntry()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var service = scope.Resolve<TemplateUsageService>();
            var memoryCache = scope.Resolve<IMemoryCache>();

            // Seed DB and cache
            var context = scope.Resolve<PersistentState>();
            var template = new TemplateUsage { Location = "ToDelete" };
            _ = context.TemplatesUsageRecords.Add(template);
            _ = await context.SaveChangesAsync(this.CancellationToken).ConfigureAwait(false);

            const string cacheKey = TemplateUsageService.CacheKeyPrefix + "ToDelete";
            _ = memoryCache.Set(cacheKey, new TemplateUsage { Location = "ToDelete", TimesUsed = 1 });

            await service.DeleteTemplateUsageAsync("ToDelete").ConfigureAwait(false);
            var cached = memoryCache.Get<TemplateUsage>(cacheKey);
            _ = cached.Should().BeNull();
        }
    }
}
