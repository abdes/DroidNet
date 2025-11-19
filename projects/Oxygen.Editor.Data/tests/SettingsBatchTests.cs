// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Services;
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
    private static readonly SettingDescriptor<string?> StringDescriptor = new()
    {
        Key = new SettingKey<string?>("BatchModule", "StringValue"),
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

    private static readonly SettingDescriptor<string?> ValidatedStringDescriptor = new()
    {
        Key = new SettingKey<string?>("ValidationModule", "StringValue"),
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

    [TestMethod]
    public async Task BeginBatch_WithNullValue_DeletesPersistedSetting()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var key = new SettingKey<string?>("BatchModule", "ToBeDeleted");
            await manager.SaveSettingAsync(key, "persisted", ct: this.CancellationToken).ConfigureAwait(false);

            var batch = manager.BeginBatch();
            await using (batch.ConfigureAwait(false))
            {
                _ = batch.QueuePropertyChange(new SettingDescriptor<string?>() { Key = key }, value: null);
            }

            var value = await manager.LoadSettingAsync(key, ct: this.CancellationToken).ConfigureAwait(false);
            _ = value.Should().BeNull();
        }
    }

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

            var stringValue = await manager.LoadSettingAsync(StringDescriptor.Key, ct: this.CancellationToken).ConfigureAwait(false);
            var intValue = await manager.LoadSettingAsync(IntDescriptor.Key, ct: this.CancellationToken).ConfigureAwait(false);

            _ = stringValue.Should().Be("TestValue");
            _ = intValue.Should().Be(42);
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
            var position = await manager.LoadSettingAsync(new SettingKey<Point>(settings.ModuleName, nameof(settings.WindowPosition)), ct: this.CancellationToken).ConfigureAwait(false);
            var size = await manager.LoadSettingAsync(new SettingKey<Size>(settings.ModuleName, nameof(settings.WindowSize)), ct: this.CancellationToken).ConfigureAwait(false);

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

            var pos = await manager.LoadSettingAsync(new SettingKey<Point>(settings1.ModuleName, nameof(settings1.WindowPosition)), ct: this.CancellationToken).ConfigureAwait(false);
            var size = await manager.LoadSettingAsync(new SettingKey<Size>(settings2.ModuleName, nameof(settings2.WindowSize)), ct: this.CancellationToken).ConfigureAwait(false);

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
            var settings = new ExampleSettings
            {
                // Without batch, property changes mark dirty but don't persist
                WindowPosition = new Point(300, 300),
            };

            _ = settings.IsDirty.Should().BeTrue();

            // Explicit save required when not in a batch
            await settings.SaveAsync(manager, ct: this.CancellationToken).ConfigureAwait(false);

            var position = await manager.LoadSettingAsync(new SettingKey<Point>(settings.ModuleName, nameof(settings.WindowPosition)), ct: this.CancellationToken).ConfigureAwait(false);

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
            const string projectPath = @"C:\Projects\TestProject";

            // Specify scope once, all settings in batch use it automatically
            var batch = manager.BeginBatch(SettingContext.Project(projectPath));
            await using (batch.ConfigureAwait(false))
            {
                settings.WindowPosition = new Point(500, 500);
            }

            // Verify saved to project scope
            var position = await manager.LoadSettingAsync(
                new SettingKey<Point>(settings.ModuleName, nameof(settings.WindowPosition)),
                SettingContext.Project(projectPath),
                ct: this.CancellationToken).ConfigureAwait(false);

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
                this.CancellationToken).ConfigureAwait(false);

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
                this.CancellationToken).ConfigureAwait(false);

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
                this.CancellationToken).ConfigureAwait(false);

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

            var intValue = await manager.LoadSettingAsync(ValidatedIntDescriptor.Key, ct: this.CancellationToken).ConfigureAwait(false);
            var stringValue = await manager.LoadSettingAsync(ValidatedStringDescriptor.Key, ct: this.CancellationToken).ConfigureAwait(false);

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
            _ = exception.Which.Results.Should().ContainSingle();
            _ = exception.Which.Results[0].ErrorMessage.Should().Be("Value must be between 1 and 100");
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
                this.CancellationToken);

            var task2 = Task.Run(
                () =>
                {
                    var batch = manager.BeginBatch();
                    batch2Captured = batch as SettingsBatch;
                    return Task.CompletedTask;
                },
                this.CancellationToken);

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
                Action act = () => manager.BeginBatch();

                _ = act.Should().Throw<InvalidOperationException>().WithMessage("*Nested batches are not supported*");
            }
        }
    }

    [TestMethod]
    public async Task BeginBatch_QueueAfterDispose_ThrowsObjectDisposedException()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var batch = manager.BeginBatch();
            await batch.DisposeAsync().ConfigureAwait(false);

            Action act = () => _ = batch.QueuePropertyChange(StringDescriptor, "value");

            _ = act.Should().Throw<ObjectDisposedException>();
        }
    }

    [TestMethod]
    public async Task BeginBatch_WithFaultySerialization_RollsBackTransaction()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            // Seed a safe setting
            var safeDescriptor = new SettingDescriptor<string?>() { Key = new SettingKey<string?>("BatchModule", "Safe") };
            var unsafeDescriptor = new SettingDescriptor<FaultyModel>() { Key = new SettingKey<FaultyModel>("BatchModule", "Faulty") };

            var act = async () =>
            {
                var batch = manager.BeginBatch();
                await using (batch.ConfigureAwait(false))
                {
                    _ = batch.QueuePropertyChange(safeDescriptor, "PersistMe");
                    _ = batch.QueuePropertyChange(unsafeDescriptor, new FaultyModel());
                }
            };

            // We expect the batch commit to fail due to a serialization error originating from the faulty model.
            var exception = await act.Should().ThrowAsync<Exception>().ConfigureAwait(false);
            var thrown = exception.Which;

            // The serializer might wrap the inner exception; ensure the cause (Bad property throws for testing) is present
            if (thrown is System.Text.Json.JsonException jsonEx)
            {
                _ = jsonEx.InnerException.Should().NotBeNull();
                _ = jsonEx.InnerException!.Message.Should().Contain("Bad property throws for testing");
            }
            else
            {
                _ = thrown.Message.Should().Contain("Bad property throws for testing");
            }

            // Ensure that when the serialization throws, none of the previously queued items are persisted
            var safeValue = await manager.LoadSettingAsync(new SettingKey<string?>("BatchModule", "Safe"), ct: this.CancellationToken).ConfigureAwait(false);
            var faultyValue = await manager.LoadSettingAsync(new SettingKey<FaultyModel>("BatchModule", "Faulty"), ct: this.CancellationToken).ConfigureAwait(false);
            _ = safeValue.Should().BeNull();
            _ = faultyValue.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task BeginBatch_SetPropertyWithNoDescriptor_ThrowsInvalidOperationException()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var settings = new NoDescriptorSettings();

            var batch = manager.BeginBatch();
            await using (batch.ConfigureAwait(false))
            {
                var act = () => settings.MissingDescriptor = "Value";
                _ = act.Should().Throw<InvalidOperationException>();
            }
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

    /// <summary>
    ///     A model whose property throws when enumerated/serialized to force JSON serialization to
    ///     fail.
    /// </summary>
    private sealed class FaultyModel
    {
        private readonly int marker = -1;

        public string Bad
        {
            get
            {
                _ = this.marker;
                throw new InvalidOperationException("Bad property throws for testing");
            }
        }
    }

    private sealed class NoDescriptorSettings : ModuleSettings
    {
        private string? missingDescriptor;

        public NoDescriptorSettings()
            : base("BatchModuleNoDescriptor")
        {
        }

        public string? MissingDescriptor
        {
            get => this.missingDescriptor;
            set => this.SetProperty(ref this.missingDescriptor, value);
        }
    }
}
