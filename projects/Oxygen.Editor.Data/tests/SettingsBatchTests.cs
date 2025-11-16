// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Tests;

/// <summary>
/// Comprehensive test suite for settings batch operations covering all scenarios from batching.md:
/// - Manual batch with descriptors
/// - Automatic batch detection with ModuleSettings
/// - Scoped batches (Application/Project)
/// - Validation with rollback
/// - Concurrency and AsyncLocal isolation
/// - Nested batch prevention
/// - Progress reporting.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Batch")]
public class SettingsBatchTests : DatabaseTests
{
    // Test descriptors for manual batch operations
    private static readonly SettingDescriptor<string> StringDescriptor = new()
    {
        Key = new SettingKey<string>("BatchModule", "StringValue"),
        DisplayName = "String Value",
    };

    private static readonly SettingDescriptor<int> IntDescriptor = new()
    {
        Key = new SettingKey<int>("BatchModule", "IntValue"),
        DisplayName = "Int Value",
    };

    // Test descriptors with validation
    private static readonly SettingDescriptor<int> ValidatedIntDescriptor = new()
    {
        Key = new SettingKey<int>("ValidationModule", "IntValue"),
        DisplayName = "Validated Integer",
        Validators =
        [
            new RangeAttribute(1, 100) { ErrorMessage = "Value must be between 1 and 100" },
        ],
    };

    private static readonly SettingDescriptor<string> ValidatedStringDescriptor = new()
    {
        Key = new SettingKey<string>("ValidationModule", "StringValue"),
        DisplayName = "Validated String",
        Validators =
        [
            new RequiredAttribute { ErrorMessage = "Value is required" },
            new StringLengthAttribute(50) { MinimumLength = 3, ErrorMessage = "Length must be between 3 and 50" },
        ],
    };

    public SettingsBatchTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task BeginBatch_ManualQueueWithDescriptors_CommitsOnDispose()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var batch = manager.BeginBatch();
            await using (batch.ConfigureAwait(false))
            {
                _ = batch.QueuePropertyChange(StringDescriptor, "TestValue");
                _ = batch.QueuePropertyChange(IntDescriptor, 42);
            }

            var stringValue = await manager.LoadSettingAsync(StringDescriptor.Key).ConfigureAwait(false);
            var intValue = await manager.LoadSettingAsync(IntDescriptor.Key).ConfigureAwait(false);

            _ = stringValue.Should().Be("TestValue");
            _ = intValue.Should().Be(42);
        }
    }

    [TestMethod]
    public async Task SaveSettingsAsync_FluentBatchApi_CommitsAtomically()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            await manager.SaveSettingsAsync(
                batch => batch
                    .QueuePropertyChange(StringDescriptor, "FluentValue")
                    .QueuePropertyChange(IntDescriptor, 99),
                null,
                CancellationToken.None).ConfigureAwait(false);

            var stringValue = await manager.LoadSettingAsync(StringDescriptor.Key).ConfigureAwait(false);
            var intValue = await manager.LoadSettingAsync(IntDescriptor.Key).ConfigureAwait(false);

            _ = stringValue.Should().Be("FluentValue");
            _ = intValue.Should().Be(99);
        }
    }

    [TestMethod]
    public async Task BeginBatch_AutomaticContextDetection_QueuesPropertiesAutomatically()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var settings = new ExampleSettings();

            var batch = manager.BeginBatch();
            await using (batch.ConfigureAwait(false))
            {
                // Property setters automatically detect batch via AsyncLocal
                settings.WindowPosition = new Point(100, 100);
                settings.WindowSize = new Size(800, 600);
            }

            // Verify persisted
            var position = await manager.LoadSettingAsync(
                new SettingKey<Point>(settings.ModuleName, nameof(settings.WindowPosition))).ConfigureAwait(false);
            var size = await manager.LoadSettingAsync(
                new SettingKey<Size>(settings.ModuleName, nameof(settings.WindowSize))).ConfigureAwait(false);

            _ = position.Should().Be(new Point(100, 100));
            _ = size.Should().Be(new Size(800, 600));
        }
    }

    [TestMethod]
    public async Task BeginBatch_MultipleSettingsInstances_AllAutoQueued()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var settings1 = new ExampleSettings();
            var settings2 = new ExampleSettings();

            var batch = manager.BeginBatch();
            await using (batch.ConfigureAwait(false))
            {
                // Both settings objects automatically queue to the same batch
                settings1.WindowPosition = new Point(100, 100);
                settings2.WindowSize = new Size(800, 600);
            }

            var pos = await manager.LoadSettingAsync(
                new SettingKey<Point>(settings1.ModuleName, nameof(settings1.WindowPosition))).ConfigureAwait(false);
            var size = await manager.LoadSettingAsync(
                new SettingKey<Size>(settings2.ModuleName, nameof(settings2.WindowSize))).ConfigureAwait(false);

            _ = pos.Should().Be(new Point(100, 100));
            _ = size.Should().Be(new Size(800, 600));
        }
    }

    [TestMethod]
    public async Task NoBatch_ImmediateDirtyMarking_RequiresExplicitSave()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var settings = new ExampleSettings();

            // Without batch, property changes mark dirty but don't persist
            settings.WindowPosition = new Point(300, 300);

            _ = settings.IsDirty.Should().BeTrue();

            // Explicit save required when not in a batch
            await settings.SaveAsync(manager).ConfigureAwait(false);

            var position = await manager.LoadSettingAsync(
                new SettingKey<Point>(settings.ModuleName, nameof(settings.WindowPosition))).ConfigureAwait(false);

            _ = position.Should().Be(new Point(300, 300));
        }
    }

    [TestMethod]
    public async Task BeginBatch_WithProjectScope_AutomaticContextPropagation()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var settings = new ExampleSettings();
            var projectPath = @"C:\Projects\TestProject";

            // Specify scope once, all settings in batch use it automatically
            var batch = manager.BeginBatch(SettingContext.Project(projectPath));
            await using (batch.ConfigureAwait(false))
            {
                settings.WindowPosition = new Point(500, 500);
            }

            // Verify saved to project scope
            var position = await manager.LoadSettingAsync(
                new SettingKey<Point>(settings.ModuleName, nameof(settings.WindowPosition)),
                SettingContext.Project(projectPath)).ConfigureAwait(false);

            _ = position.Should().Be(new Point(500, 500));
        }
    }

    [TestMethod]
    public async Task BeginBatch_SequentialScopedBatches_IsolatedCorrectly()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            // Use Task.Run for proper AsyncLocal isolation between sequential batches
            await Task.Run(
                async () =>
                {
                    var settings1 = new ExampleSettings();
                    var batch1 = manager.BeginBatch(SettingContext.Project("Project1"));
                    await using (batch1.ConfigureAwait(false))
                    {
                        settings1.WindowPosition = new Point(10, 10);
                        settings1.WindowSize = new Size(100, 100);
                    }
                },
                this.TestContext.CancellationToken).ConfigureAwait(false);

            await Task.Run(
                async () =>
                {
                    var settings2 = new ExampleSettings();
                    var batch2 = manager.BeginBatch(SettingContext.Project("Project2"));
                    await using (batch2.ConfigureAwait(false))
                    {
                        settings2.WindowPosition = new Point(20, 20);
                        settings2.WindowSize = new Size(200, 200);
                    }
                },
                this.TestContext.CancellationToken).ConfigureAwait(false);

            await Task.Run(
                async () =>
                {
                    var settings3 = new ExampleSettings();
                    var batch3 = manager.BeginBatch(SettingContext.Application());
                    await using (batch3.ConfigureAwait(false))
                    {
                        settings3.WindowPosition = new Point(30, 30);
                        settings3.WindowSize = new Size(300, 300);
                    }
                },
                this.TestContext.CancellationToken).ConfigureAwait(false);

            // Verify all batches committed to their respective scopes
            var (pos1, size1) = await LoadSettingsAsync(manager, SettingContext.Project("Project1")).ConfigureAwait(false);
            var (pos2, size2) = await LoadSettingsAsync(manager, SettingContext.Project("Project2")).ConfigureAwait(false);
            var (pos3, size3) = await LoadSettingsAsync(manager, SettingContext.Application()).ConfigureAwait(false);

            _ = pos1.Should().Be(new Point(10, 10));
            _ = size1.Should().Be(new Size(100, 100));
            _ = pos2.Should().Be(new Point(20, 20));
            _ = size2.Should().Be(new Size(200, 200));
            _ = pos3.Should().Be(new Point(30, 30));
            _ = size3.Should().Be(new Size(300, 300));
        }
    }

    [TestMethod]
    public async Task BeginBatch_WithValidValues_CommitsSuccessfully()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var batch = manager.BeginBatch();
            await using (batch.ConfigureAwait(false))
            {
                _ = batch.QueuePropertyChange(ValidatedIntDescriptor, 50);
                _ = batch.QueuePropertyChange(ValidatedStringDescriptor, "Valid Value");
            }

            var intValue = await manager.LoadSettingAsync(ValidatedIntDescriptor.Key).ConfigureAwait(false);
            var stringValue = await manager.LoadSettingAsync(ValidatedStringDescriptor.Key).ConfigureAwait(false);

            _ = intValue.Should().Be(50);
            _ = stringValue.Should().Be("Valid Value");
        }
    }

    [TestMethod]
    public async Task BeginBatch_WithInvalidValue_ThrowsAndRollsBack()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var act = async () =>
            {
                var batch = manager.BeginBatch();
                await using (batch.ConfigureAwait(false))
                {
                    _ = batch.QueuePropertyChange(ValidatedIntDescriptor, 150); // Out of range
                }
            };

            var exception = await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(false);
            _ = exception.Which.Message.Should().Contain("Batch validation failed");
            _ = exception.Which.Results.Should().HaveCount(1);
            _ = exception.Which.Results[0].ErrorMessage.Should().Be("Value must be between 1 and 100");
        }
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WithMultipleInvalidValues_AggregatesErrors()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var act = async () => await manager.SaveSettingsAsync(
                batch => batch
                    .QueuePropertyChange(ValidatedIntDescriptor, 200) // Out of range
                    .QueuePropertyChange(ValidatedStringDescriptor, "AB"), // Too short
                null,
                CancellationToken.None).ConfigureAwait(false);

            var exception = await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(false);
            _ = exception.Which.Results.Should().HaveCount(2);
            var messages = exception.Which.Results.Select(r => r.ErrorMessage).ToList();
            _ = messages.Should().Contain("Value must be between 1 and 100");
            _ = messages.Should().Contain("Length must be between 3 and 50");
        }
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WithMixedValidAndInvalid_RollsBackAll()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var act = async () => await manager.SaveSettingsAsync(
                batch => batch
                    .QueuePropertyChange(ValidatedIntDescriptor, 25) // Valid
                    .QueuePropertyChange(ValidatedStringDescriptor, "XX"), // Invalid
                null,
                CancellationToken.None).ConfigureAwait(false);

            _ = await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(false);

            // Verify nothing was saved (atomic rollback)
            var intValue = await manager.LoadSettingAsync(ValidatedIntDescriptor.Key).ConfigureAwait(false);
            var stringValue = await manager.LoadSettingAsync(ValidatedStringDescriptor.Key).ConfigureAwait(false);

            _ = intValue.Should().Be(0); // Default value
            _ = stringValue.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task BeginBatch_AsyncLocalIsolation_DifferentExecutionContexts()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            SettingsBatch? batch1Captured = null;
            SettingsBatch? batch2Captured = null;

            // Each Task.Run gets its own ExecutionContext, so AsyncLocal isolates
            var task1 = Task.Run(
                () =>
                {
                    var batch = manager.BeginBatch();
                    batch1Captured = batch as SettingsBatch;
                    return Task.CompletedTask;
                },
                this.TestContext.CancellationToken);

            var task2 = Task.Run(
                () =>
                {
                    var batch = manager.BeginBatch();
                    batch2Captured = batch as SettingsBatch;
                    return Task.CompletedTask;
                },
                this.TestContext.CancellationToken);

            await Task.WhenAll(task1, task2).ConfigureAwait(false);

            // Each thread created its own batch instance
            _ = batch1Captured.Should().NotBeNull();
            _ = batch2Captured.Should().NotBeNull();
            _ = batch1Captured.Should().NotBeSameAs(batch2Captured);

            // Clean up
            if (batch1Captured != null)
            {
                await batch1Captured.DisposeAsync().ConfigureAwait(false);
            }

            if (batch2Captured != null)
            {
                await batch2Captured.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    [TestMethod]
    public async Task BeginBatch_NestedBatches_ThrowsInvalidOperationException()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var outerBatch = manager.BeginBatch();
            await using (outerBatch.ConfigureAwait(false))
            {
                // Attempting to create a nested batch should throw
                var act = () => manager.BeginBatch();

                _ = act.Should().Throw<InvalidOperationException>()
                    .Which.Message.Should().Contain("Nested batches are not supported");
            }
        }
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WithProgressReporting_ReportsCorrectly()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var progressReports = new List<SettingsSaveProgress>();

            await manager.SaveSettingsAsync(
                batch => batch
                    .QueuePropertyChange(ValidatedIntDescriptor, 30)
                    .QueuePropertyChange(ValidatedStringDescriptor, "Progress Test"),
                new Progress<SettingsSaveProgress>(p => progressReports.Add(p)),
                CancellationToken.None).ConfigureAwait(false);

            _ = progressReports.Should().NotBeEmpty();
            _ = progressReports[^1].TotalSettings.Should().Be(2);
            _ = progressReports[^1].CompletedSettings.Should().Be(2);
        }
    }

    private static async Task<(Point position, Size size)> LoadSettingsAsync(EditorSettingsManager manager, SettingContext context)
    {
        var pos = await manager.LoadSettingAsync(
            new SettingKey<Point>("Oxygen.Editor.Data.Example", "WindowPosition"),
            context).ConfigureAwait(false);
        var size = await manager.LoadSettingAsync(
            new SettingKey<Size>("Oxygen.Editor.Data.Example", "WindowSize"),
            context).ConfigureAwait(false);
        return (pos, size);
    }
}
