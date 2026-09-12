// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Exercises the managed runtime boundary in isolation inside the existing test assembly.</summary>
[TestClass]
public sealed class ManagedRuntimeBoundaryTests
{
    /// <summary>Managed construction and failed startup never load Interop, even if it is available elsewhere in the process.</summary>
    /// <returns>The asynchronous boundary test.</returns>
    [TestMethod]
    public async Task RuntimeSettingsAndServiceRemainUsableWithoutInterop()
    {
        var context = new RuntimeWithoutInterop();
        try
        {
            var assembly = context.LoadFromAssemblyPath(typeof(IEngineService).Assembly.Location);
            foreach (var type in assembly.GetExportedTypes())
            {
                foreach (var method in type.GetMethods())
                {
                    _ = method.ReturnType.Assembly.GetName().Name.Should().NotBe("DroidNet.Oxygen.Editor.Interop");
                    _ = method.GetParameters().Should().NotContain(parameter => parameter.ParameterType.Assembly.GetName().Name == "DroidNet.Oxygen.Editor.Interop");
                }
            }

            var settingsType = assembly.GetType(typeof(EngineSettings).FullName!, throwOnError: true)!;
            _ = JsonSerializer.Serialize(Activator.CreateInstance(settingsType), settingsType).Should().NotBeEmpty();
            var diagnostic = new DiagnosticRecord { OperationId = Guid.NewGuid(), Domain = FailureDomain.RuntimeDiscovery, Severity = DiagnosticSeverity.Error, Code = NativeCompatibilityDiagnosticCodes.ArtifactMismatch, Message = "SDK changed" };
            var compatibility = new Mock<INativeCompatibilityService>();
            _ = compatibility.Setup(value => value.VerifyAsync(It.IsAny<Guid>(), It.IsAny<CancellationToken>())).ReturnsAsync(new NativeCompatibilityResult(Artifacts: null, [diagnostic]));
            var hosting = new HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! };
            var serviceType = assembly.GetType(typeof(EngineService).FullName!, throwOnError: true)!;
            var service = (IAsyncDisposable)Activator.CreateInstance(serviceType, hosting, Mock.Of<IOperationResultPublisher>(), null, null, null, compatibility.Object)!;
            await using var lifetime = service.ConfigureAwait(false);
            var initialize = (ValueTask<bool>)serviceType.GetMethod(nameof(EngineService.InitializeAsync))!.Invoke(service, [CancellationToken.None])!;
            Func<Task> start = initialize.AsTask;
            _ = await start.Should().ThrowAsync<NativeCompatibilityException>().ConfigureAwait(false);
            _ = serviceType.GetProperty(nameof(EngineService.State))!.GetValue(service)!.ToString().Should().Be(nameof(EngineServiceState.Faulted));
            _ = context.InteropRequested.Should().BeFalse();
        }
        finally
        {
            context.Unload();
        }
    }

    private sealed class RuntimeWithoutInterop() : AssemblyLoadContext(isCollectible: true)
    {
        public bool InteropRequested { get; private set; }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (string.Equals(assemblyName.Name, "DroidNet.Oxygen.Editor.Interop", StringComparison.Ordinal))
            {
                this.InteropRequested = true;
                throw new FileNotFoundException("Interop is unavailable in this test context.");
            }

            return Default.LoadFromAssemblyName(assemblyName);
        }
    }
}
