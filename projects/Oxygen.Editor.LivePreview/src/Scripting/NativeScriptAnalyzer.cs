// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Runtime.CompilerServices;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core.Diagnostics;
using Native = Oxygen.Interop.Scripting;

namespace Oxygen.Editor.LivePreview.Scripting;

/// <summary>
/// The engine's script analyzer, loaded from the engine SDK: it reads a script's declarations and problems without
/// running or compiling it.
/// </summary>
/// <remarks>
/// The analyzer is native. The engine service makes the SDK loadable once it has verified it, so analyses wait until
/// then. When the analyzer cannot be loaded, each analysis reports itself unavailable.
/// </remarks>
public sealed partial class NativeScriptAnalyzer : IScriptAnalyzer, IDisposable
{
    private readonly IEngineService engine;
    private readonly TaskCompletionSource ready = new(TaskCreationOptions.RunContinuationsAsynchronously);

    /// <summary>Initializes a new instance of the <see cref="NativeScriptAnalyzer"/> class.</summary>
    /// <param name="engine">The engine service, which makes the native SDK loadable.</param>
    public NativeScriptAnalyzer(IEngineService engine)
    {
        ArgumentNullException.ThrowIfNull(engine);
        this.engine = engine;
        engine.StateChanged += this.OnEngineStateChanged;
        this.OnEngineStateChanged(engine.State);
    }

    /// <inheritdoc />
    public bool IsAvailable => this.ready.Task.IsCompleted;

    /// <inheritdoc />
    public Task WaitUntilAvailableAsync(CancellationToken cancellationToken) => this.ready.Task.WaitAsync(cancellationToken);

    /// <inheritdoc />
    public ScriptAnalysis Analyze(string sourceName, ReadOnlySpan<byte> source, string? definitions)
    {
        ArgumentNullException.ThrowIfNull(sourceName);
        var bytes = source.ToArray();
        try
        {
            return AnalyzeNative(sourceName, bytes, definitions);
        }
        catch (Exception exception) when (exception is FileNotFoundException or DllNotFoundException or BadImageFormatException or TypeLoadException or FileLoadException or System.Runtime.InteropServices.SEHException)
        {
            return ScriptAnalysis.Unavailable(exception.Message);
        }
    }

    /// <inheritdoc />
    public void Dispose() => this.engine.StateChanged -= this.OnEngineStateChanged;

    /// <summary>Maps a parameter's declared default to the value a script slot stores.</summary>
    /// <param name="value">The default: bool, int, float, string, Vector2, Vector3 or Vector4.</param>
    /// <returns>The value; <see langword="null"/> for any other default, such as an asset or node parameter's.</returns>
    internal static ScriptValue? ToValue(object? value)
        => value switch
        {
            bool flag => ScriptValue.FromBool(flag),
            int number => ScriptValue.FromInt(number),
            float number => ScriptValue.FromFloat(number),
            string text => ScriptValue.FromString(text),
            Vector2 vector => ScriptValue.FromVector(vector),
            Vector3 vector => ScriptValue.FromVector(vector),
            Vector4 vector => ScriptValue.FromVector(vector),
            _ => null,
        };

    private static DiagnosticSeverity ToSeverity(Native.ScriptDiagnosticSeverity severity) => severity switch
    {
        Native.ScriptDiagnosticSeverity.Error => DiagnosticSeverity.Error,
        Native.ScriptDiagnosticSeverity.Warning => DiagnosticSeverity.Warning,
        _ => DiagnosticSeverity.Info,
    };

    // Loading the Interop assembly fails when the method that uses its types is compiled, so those types stay here
    // and the load failure surfaces at its call.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static ScriptAnalysis AnalyzeNative(string sourceName, byte[] source, string? definitions)
    {
        var analysis = Native.ScriptAnalysis.Analyze(sourceName, source, definitions is null ? Native.ScriptAnalysisLevel.Lint : Native.ScriptAnalysisLevel.Types, definitions);
        return new ScriptAnalysis
        {
            HasDeclaration = analysis.HasDeclaration,
            Problems = [.. analysis.Diagnostics.Select(static diagnostic => new ScriptProblem(ToSeverity(diagnostic.Severity), diagnostic.Code, diagnostic.Message, diagnostic.Rendered)
            {
                Line = diagnostic.PrimarySpan?.Line,
                Column = diagnostic.PrimarySpan?.Column,
                Explanation = diagnostic.Explanation,
            })],
            Parameters = [.. analysis.Parameters.Select(static parameter => new ScriptParameter
            {
                Name = parameter.Name,
                Kind = (ScriptParameterKind)(int)parameter.Type,
                Default = ToValue(parameter.DefaultValue),
                AssetType = parameter.AssetType,
                Min = parameter.Min,
                Max = parameter.Max,
                Step = parameter.Step,
                Unit = parameter.Unit ?? string.Empty,
                Description = parameter.Description ?? string.Empty,
                Options = parameter.Options ?? [],
                IsColor = parameter.IsColor,
            })],
        };
    }

    private void OnEngineStateChanged(object? sender, EngineStateChangedEventArgs args) => this.OnEngineStateChanged(args.State);

    private void OnEngineStateChanged(EngineServiceState state)
    {
        // Ready and later: the engine service has verified the SDK and put its binaries on the search path.
        if (state is EngineServiceState.Ready or EngineServiceState.Starting or EngineServiceState.Running)
        {
            _ = this.ready.TrySetResult();
        }
    }
}
