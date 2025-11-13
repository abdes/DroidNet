// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Reflection;
using DroidNet.Config.Tests.Helpers;
using DryIoc;
using AwesomeAssertions;

namespace DroidNet.Config.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Manager - AutoSave")]
public class SettingsManagerAutoSaveTests : SettingsTestBase
{
    private readonly MockSettingsSource source = new("test-source");

    [TestInitialize]
    public void TestInitialize()
    {
        this.Container.RegisterInstance<ISettingsSource>(this.source);

        // Enable test logging for troubleshooting
        this.EnableTestLogging = false;  // Set to true to see debug output
        this.SetupTestLogging(this.TestContext);
    }

    [TestMethod]
    public async Task AutoSave_ShouldSaveDirtyService_AfterDelay()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Enable AutoSave with short delay
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(100);
        this.SettingsManager.AutoSave = true;

        // Mark service dirty and trigger notification
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Wait long enough for debounce + save
        await Task.Delay(400, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - source should have been written to
        _ = this.source.WriteCallCount.Should().Be(1);

        // Service should be marked clean
        _ = service.IsDirty.Should().BeFalse();

        // Disable AutoSave to clean up
        this.SettingsManager.AutoSave = false;
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_ToggleOff_ShouldSaveImmediately()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Use long delay so automatic debounce would not have fired yet
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromSeconds(5);
        this.SettingsManager.AutoSave = true;

        // Mark dirty and notify
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Immediately toggle off - this should cause immediate save
        this.SettingsManager.AutoSave = false;

        // Give time for the stop/save task to complete
        await Task.Delay(400, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = this.source.WriteCallCount.Should().Be(1);
        _ = service.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task AutoSave_ToggleOff_WhenSaveFails_ShouldNotMarkClean()
    {
        // Arrange - configure failing source as the winner for the section
        var failingSource = new MockSettingsSource("failing-source") { ShouldFailWrite = true };
        failingSource.AddSection(nameof(TestSettings), new TestSettings { Name = "FromFailing", Value = 10 });
        this.Container.RegisterInstance<ISettingsSource>(failingSource);

        // Re-initialize manager so failing source is included
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Use long delay so automatic debounce would not have fired yet
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromSeconds(5);
        this.SettingsManager.AutoSave = true;

        // Mark dirty and notify
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Immediately toggle off - this should attempt an immediate save which will fail
        this.SettingsManager.AutoSave = false;

        // Give time for the stop/save task to complete
        await Task.Delay(400, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - failing source attempted write and service remains dirty
        _ = failingSource.WriteCallCount.Should().BeGreaterThanOrEqualTo(1);
        _ = service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task AutoSave_Debounce_MultipleChanges_OneSave()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(200);
        this.SettingsManager.AutoSave = true;

        // Rapidly mark dirty multiple times within debounce window
        for (var i = 0; i < 3; i++)
        {
            _ = service.Should().BeOfType<TestSettingsService>();
            ((TestSettingsService)service).SetTestIsDirty(value: true);

            TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        // Wait for debounce window to elapse and allow save to complete
        await Task.Delay(600, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Only one write should have occurred due to debouncing
        _ = this.source.WriteCallCount.Should().Be(1);
        _ = service.IsDirty.Should().BeFalse();

        this.SettingsManager.AutoSave = false;
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_PendingSave_ExecutesAfterCurrentSaveCompletes()
    {
        // Arrange - use a slow source to simulate a long save operation
        var slowSource = new SlowMockSettingsSource("slow-source", saveDelayMs: 500);
        slowSource.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        this.Container.RegisterInstance<ISettingsSource>(slowSource);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(50);
        this.SettingsManager.AutoSave = true;

        var callbackFired = false;

        // Set up callback to modify property DURING the save operation
        // This ensures the snapshot changes between before and after, keeping service dirty
        slowSource.OnSaveDelayCallback = () =>
        {
            if (!callbackFired)
            {
                callbackFired = true;
                ((ITestSettings)service).Value = 200;  // Change during save - makes snapshot different
            }

            return Task.CompletedTask;
        };

        // Trigger first save
        ((ITestSettings)service).Value = 100;

        // Wait for debounce + save with callback + pending save to execute
        // Save is 500ms, debounce is 50ms, so: 50 + 500 + 50 + 500 = 1100ms minimum
        await Task.Delay(1500, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - two saves should have occurred:
        // 1. First save with Value=100, but callback changes it to 200 during save
        // 2. Pending save persists Value=200
        _ = slowSource.WriteCallCount.Should().Be(2);
        _ = service.IsDirty.Should().BeFalse();
        _ = ((ITestSettings)service).Value.Should().Be(200);

        this.SettingsManager.AutoSave = false;
        await Task.Delay(200, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_MultiplePendingSaves_CoalescesToSinglePendingSave()
    {
        // Arrange - use a slow source
        var slowSource = new SlowMockSettingsSource("slow-source", saveDelayMs: 600);
        slowSource.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        this.Container.RegisterInstance<ISettingsSource>(slowSource);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(50);
        this.SettingsManager.AutoSave = true;

        var callbackFired = false;

        // Set up callback to modify property DURING the first save operation only
        slowSource.OnSaveDelayCallback = () =>
        {
            if (!callbackFired)
            {
                callbackFired = true;
                ((ITestSettings)service).Value = 201;  // Change during first save
            }

            return Task.CompletedTask;
        };

        // Trigger first save
        ((ITestSettings)service).Value = 100;

        // Wait for debounce to trigger first save
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Now first save is running and callback will change value to 201 during it.
        // Trigger multiple property changes rapidly while save is running.
        // Each will schedule a debounce timer that cancels the previous one.
        // When the last debounce timer fires, it will set hasPendingSave = true (boolean flag).
        // The first save will complete with snapshot mismatch (100 vs 201), keeping service dirty.
        // The pending save will execute and callback will change to 202 during it.
        for (var i = 0; i < 5; i++)
        {
            // Change property to trigger dirty
            var message = string.Format(CultureInfo.InvariantCulture, "Write count: {0}", this.source.WriteCallCount);
            await Task.Delay(20, this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        // Wait for all operations to complete
        // Multiple debounce cycles (50ms each) + multiple saves (600ms each)
        await Task.Delay(2500, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - should be exactly 2 saves: the initial one + 1 pending save
        // (not 6, because multiple pending requests coalesce into a single pending flag)
        _ = slowSource.WriteCallCount.Should().Be(2);
        _ = service.IsDirty.Should().BeFalse();
        _ = ((ITestSettings)service).Value.Should().Be(201);  // Value from callback

        this.SettingsManager.AutoSave = false;
        await Task.Delay(200, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_BeforeInitialize_StartsAfterInitialize()
    {
        // Arrange - set AutoSave before initialization
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(50);
        this.SettingsManager.AutoSave = true;

        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });

        // Act - Initialize (manager should start autosave because property is true)
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Mark dirty and trigger notification
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Wait for save to occur
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = this.source.WriteCallCount.Should().Be(1);
        _ = service.IsDirty.Should().BeFalse();

        this.SettingsManager.AutoSave = false;
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_ShouldNotSave_WhenServiceIsBusy()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(100);
        this.SettingsManager.AutoSave = true;

        // Mark service busy and dirty
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsBusy(value: true);
        ((TestSettingsService)service).SetTestIsDirty(value: true);

        // Trigger notification
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Wait longer than delay
        await Task.Delay(400, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - no save should have occurred and service remains dirty
        _ = this.source.WriteCallCount.Should().Be(0);
        _ = service.IsDirty.Should().BeTrue();

        this.SettingsManager.AutoSave = false;
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_WhenSaveFails_ShouldNotMarkServiceClean()
    {
        // Arrange - configure source to fail writes
        var failingSource = new MockSettingsSource("failing-source") { ShouldFailWrite = true };
        failingSource.AddSection(nameof(TestSettings), new TestSettings { Name = "FromFailing", Value = 10 });
        this.Container.RegisterInstance<ISettingsSource>(failingSource);

        // Re-initialize manager with the failing source included
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Enable autosave
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(100);
        this.SettingsManager.AutoSave = true;

        // Mark dirty and trigger
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Wait for autosave attempt
        await Task.Delay(400, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - write was attempted but service remains dirty because write failed
        _ = failingSource.WriteCallCount.Should().BeGreaterThanOrEqualTo(1);
        _ = service.IsDirty.Should().BeTrue();

        this.SettingsManager.AutoSave = false;
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_DisposeDuringOngoingSave_WaitsForSaveToComplete()
    {
        // Arrange - use a slow source so save takes time
        var slowSource = new SlowMockSettingsSource("slow-source", saveDelayMs: 300);
        slowSource.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        this.Container.RegisterInstance<ISettingsSource>(slowSource);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(50);
        this.SettingsManager.AutoSave = true;

        // Mark dirty and trigger
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Wait for debounce to trigger save, but don't wait for save to complete
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Dispose while save is ongoing - should wait for save to complete
        this.SettingsManager.Dispose();

        // Assert - save should have completed (not cancelled)
        _ = slowSource.WriteCallCount.Should().Be(1);
        _ = service.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task AutoSave_DisposeDuringPendingDebounce_DoesNotSave()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Set a long debounce so the save won't happen immediately
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromSeconds(5);
        this.SettingsManager.AutoSave = true;

        // Mark dirty and trigger
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsDirty));

        // Dispose manager immediately to cancel pending debounce timer
        this.SettingsManager.Dispose();

        // Wait to allow any background tasks to run if they were scheduled
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - no writes should have occurred
        _ = this.source.WriteCallCount.Should().Be(0);
        _ = service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task AutoSave_SetSameValue_NoOp()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - enable AutoSave
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(50);
        this.SettingsManager.AutoSave = true;

        // Capture current AutoSaver instance
        var autoSaverField = typeof(SettingsManager).GetField("autoSaver", BindingFlags.Instance | BindingFlags.NonPublic);
        var firstAutoSaver = autoSaverField?.GetValue(this.SettingsManager);

        // Act again with same value
        this.SettingsManager.AutoSave = true;

        var secondAutoSaver = autoSaverField?.GetValue(this.SettingsManager);

        // Assert - instance should be unchanged (no restart)
        _ = firstAutoSaver.Should().BeSameAs(secondAutoSaver);

        // Cleanup
        this.SettingsManager.AutoSave = false;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public void AutoSaveDelay_NonPositive_Throws()
    {
        // Arrange & Act / Assert
        Action setZero = () => this.SettingsManager.AutoSaveDelay = TimeSpan.Zero;
        Action setNegative = () => this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(-1);

        _ = setZero.Should().Throw<ArgumentOutOfRangeException>();
        _ = setNegative.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public async Task AutoSaveProperty_Setter_UpdatesValue()
    {
        // Arrange - ensure manager is initialized
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - set AutoSave on and verify
        this.SettingsManager.AutoSave = true;

        // Assert - property reflects set value
        _ = this.SettingsManager.AutoSave.Should().BeTrue();

        // Cleanup
        this.SettingsManager.AutoSave = false;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_ToggleOn_WhenServiceAlreadyDirty_ShouldSaveImmediately()
    {
        // Arrange - add a section and initialize
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Mark service dirty BEFORE enabling AutoSave
        _ = service.Should().BeOfType<TestSettingsService>();
        ((TestSettingsService)service).SetTestIsDirty(value: true);

        // Act - enable AutoSave with short delay and ensure it triggers immediate save for existing dirty
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(50);
        this.SettingsManager.AutoSave = true;

        // Give time for the immediate save to run
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - source should have been written and service should be clean
        _ = this.source.WriteCallCount.Should().Be(1);
        _ = service.IsDirty.Should().BeFalse();

        // Cleanup
        this.SettingsManager.AutoSave = false;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_Reload_ShouldNotTriggerSave()
    {
        // Arrange - add a section and initialize
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Enable AutoSave
        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(50);
        this.SettingsManager.AutoSave = true;

        // Ensure service is clean initially
        _ = service.IsDirty.Should().BeFalse();

        // Simulate a source reload - this should update services but not trigger AutoSave
        this.source.TriggerSourceChanged(SourceChangeType.Updated);

        // Wait to allow reload handling to complete
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - no save should have occurred as a result of reload
        _ = this.source.WriteCallCount.Should().Be(0);

        // Cleanup
        this.SettingsManager.AutoSave = false;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public void AutoSaveDelayProperty_Setter_UpdatesValue()
    {
        // Arrange
        var newDelay = TimeSpan.FromMilliseconds(123);

        // Act
        this.SettingsManager.AutoSaveDelay = newDelay;

        // Assert
        _ = this.SettingsManager.AutoSaveDelay.Should().Be(newDelay);
    }

    [TestMethod]
    public async Task AutoSave_NoDirtyServices_NoSaveOccurs()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(100);
        this.SettingsManager.AutoSave = true;

        // Service is NOT dirty - just wait
        await Task.Delay(400, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - no save should occur
        _ = this.source.WriteCallCount.Should().Be(0);

        this.SettingsManager.AutoSave = false;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    [TestMethod]
    public async Task AutoSave_PropertyChangedForNonDirtyProperty_Ignored()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 1 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        this.SettingsManager.AutoSaveDelay = TimeSpan.FromMilliseconds(100);
        this.SettingsManager.AutoSave = true;

        // Trigger property changed for a different property (not IsDirty)
        TriggerAutoSaverPropertyChanged(this.SettingsManager, service, nameof(ISettingsService.IsBusy));

        // Wait
        await Task.Delay(400, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - no save should occur
        _ = this.source.WriteCallCount.Should().Be(0);

        this.SettingsManager.AutoSave = false;
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    // Helper: find private AutoSaver instance and invoke its OnServicePropertyChanged to simulate the notification
    private static void TriggerAutoSaverPropertyChanged(SettingsManager manager, ISettingsService service, string propertyName)
    {
        var autoSaverField = typeof(SettingsManager).GetField("autoSaver", BindingFlags.Instance | BindingFlags.NonPublic);
        var autoSaver = autoSaverField?.GetValue(manager);
        if (autoSaver is null)
        {
            return;
        }

        var method = autoSaver.GetType().GetMethod("OnServicePropertyChanged", BindingFlags.Instance | BindingFlags.NonPublic);
        _ = method?.Invoke(autoSaver, [service, new PropertyChangedEventArgs(propertyName)]);
    }
}
