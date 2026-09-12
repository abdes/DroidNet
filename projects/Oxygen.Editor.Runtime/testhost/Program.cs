// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text.Json;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.ManagedBoundaryProbe;

/// <summary>Exercises startup-facing runtime code in an installation without Interop.</summary>
internal static class Program
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Globalization", "CA1303:Do not pass literals as localized parameters", Justification = "The probe emits a fixed protocol marker consumed by the automated test.")]
    private static async Task<int> Main()
    {
        AssemblyLoadContext.Default.Resolving += RejectInterop;
        await ExerciseManagedRuntimeAsync().ConfigureAwait(false);
        if (AppDomain.CurrentDomain.GetAssemblies().Any(static assembly => string.Equals(assembly.GetName().Name, "DroidNet.Oxygen.Editor.Interop", StringComparison.Ordinal)))
        {
            throw new InvalidOperationException("Interop was loaded before native startup.");
        }

        Console.WriteLine("Managed runtime ready; Interop not loaded.");
        return 0;
    }

    private static Assembly? RejectInterop(AssemblyLoadContext context, AssemblyName name)
        => string.Equals(name.Name, "DroidNet.Oxygen.Editor.Interop", StringComparison.Ordinal) ? throw new InvalidOperationException("INTEROP_LOAD_REQUESTED") : null;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static async Task ExerciseManagedRuntimeAsync()
    {
        _ = JsonSerializer.Serialize(new EngineSettings());
        _ = new RuntimeViewConfig { Name = "Offline viewport", ClearColor = new(0.1f, 0.2f, 0.3f, 1.0f) };
        InspectPublicContracts();
        var hosting = new HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! };
        var publisher = new Publisher();
        var engine = new EngineService(hosting, publisher);
        await using var lifetime = engine.ConfigureAwait(false);
        if (engine.WorldCommands.RunId != Guid.Empty)
        {
            throw new InvalidOperationException("The managed service unexpectedly started native work.");
        }

        var result = await engine.WorldCommands.ActivateSceneAsync(Guid.NewGuid(), default, "Offline scene").ConfigureAwait(false);
        if (result.Status != RuntimeCommandStatus.Unavailable)
        {
            throw new InvalidOperationException("Unavailable native work was not reported explicitly.");
        }

        try
        {
            _ = await engine.InitializeAsync().ConfigureAwait(false);
            throw new InvalidOperationException("Unqualified native startup was allowed.");
        }
        catch (ArtifactQualificationException exception)
        {
            if (engine.State != EngineServiceState.Faulted || exception.Diagnostics.IsEmpty || !string.Equals(publisher.Result?.OperationKind, "Runtime.Qualification", StringComparison.Ordinal))
            {
                throw new InvalidOperationException("Qualification failure was not reported through the runtime state and diagnostics.");
            }
        }
    }

    private static void InspectPublicContracts()
    {
        var visited = new HashSet<Type>();
        foreach (var type in typeof(IEngineService).Assembly.GetExportedTypes())
        {
            Inspect(type);
        }

        void Inspect(Type type)
        {
            if (!visited.Add(type))
            {
                return;
            }

            if (string.Equals(type.Assembly.GetName().Name, "DroidNet.Oxygen.Editor.Interop", StringComparison.Ordinal))
            {
                throw new InvalidOperationException($"Native type escaped the runtime boundary: {type.FullName}");
            }

            foreach (var argument in type.GetGenericArguments())
            {
                Inspect(argument);
            }

            if (type.Assembly == typeof(IEngineService).Assembly)
            {
                foreach (var method in type.GetMethods())
                {
                    Inspect(method.ReturnType);
                    foreach (var parameter in method.GetParameters())
                    {
                        Inspect(parameter.ParameterType);
                    }
                }
            }
        }
    }

    private sealed class Publisher : IOperationResultPublisher
    {
        public OperationResult? Result { get; private set; }

        public void Publish(OperationResult result) => this.Result = result;

        public IDisposable Subscribe(IObserver<OperationResult> observer) => System.Reactive.Disposables.Disposable.Empty;
    }
}
