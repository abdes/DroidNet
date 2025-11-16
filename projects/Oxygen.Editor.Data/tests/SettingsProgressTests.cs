// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Progress")]
public class SettingsProgressTests : DatabaseTests
{
    public SettingsProgressTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task SaveSettingAsync_ShouldReportProgress_ForSingleSave()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var key = new SettingKey<string?>("ProgressModule", "SingleSave");
            var reports = new List<SettingsProgress>();
            var progress = new SynchronousProgress<SettingsProgress>(reports.Add);

            await mgr.SaveSettingAsync(key, "value", SettingContext.Application(), progress: progress, ct: this.CancellationToken).ConfigureAwait(false);

            _ = reports.Should().ContainSingle();
            _ = reports[0].TotalSettings.Should().Be(1);
            _ = reports[0].CompletedSettings.Should().Be(1);
            _ = reports[0].SettingModule.Should().Be("ProgressModule");
            _ = reports[0].SettingName.Should().Be("SingleSave");
        }
    }

    [TestMethod]
    public async Task GetAllValuesAsync_ShouldReportProgress_ForMultipleLoads()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var key = new SettingKey<string?>("ProgressModule", "MultiLoad");

            await mgr.SaveSettingAsync(key, "a", SettingContext.Application(), ct: this.CancellationToken).ConfigureAwait(false);
            await mgr.SaveSettingAsync(key, "b", SettingContext.Project("proj1"), ct: this.CancellationToken).ConfigureAwait(false);

            var reports = new List<SettingsProgress>();
            var progress = new SynchronousProgress<SettingsProgress>(reports.Add);

            var all = await mgr.GetAllValuesAsync("ProgressModule/MultiLoad", progress: progress, ct: this.CancellationToken).ConfigureAwait(false);
            _ = all.Should().HaveCount(2);
            _ = reports.Should().HaveCount(2);
            _ = reports[^1].CompletedSettings.Should().Be(reports[^1].TotalSettings);
            _ = reports[^1].SettingModule.Should().Be("ProgressModule");
            _ = reports[^1].SettingName.Should().Be("MultiLoad");
        }
    }

    [TestMethod]
    public async Task TryGetAllValuesAsync_ShouldReportProgress_ForMultipleLoads()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var key = new SettingKey<string?>("ProgressModule", "TryMultiLoad");

            await mgr.SaveSettingAsync(key, "a", SettingContext.Application(), ct: this.CancellationToken).ConfigureAwait(false);
            await mgr.SaveSettingAsync(key, "b", SettingContext.Project("proj1"), ct: this.CancellationToken).ConfigureAwait(false);

            var reports = new List<SettingsProgress>();
            var progress = new SynchronousProgress<SettingsProgress>(reports.Add);

            var res = await mgr.TryGetAllValuesAsync<string>("ProgressModule/TryMultiLoad", progress: progress, ct: this.CancellationToken).ConfigureAwait(false);
            _ = res.Success.Should().BeTrue();
            _ = reports.Should().HaveCount(2);
            _ = reports[^1].CompletedSettings.Should().Be(reports[^1].TotalSettings);
            _ = reports[^1].SettingModule.Should().Be("ProgressModule");
            _ = reports[^1].SettingName.Should().Be("TryMultiLoad");
        }
    }

    private sealed class SynchronousProgress<T>(Action<T> callback) : IProgress<T>
    {
        private readonly Action<T> callback = callback;

        public void Report(T value) => this.callback(value);
    }
}
