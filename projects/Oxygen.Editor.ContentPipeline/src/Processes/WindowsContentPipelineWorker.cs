// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Oxygen.Editor.ContentPipeline.Processes;

/// <summary>Owns a worker job from atomic process creation through tree exit and stream drain.</summary>
internal sealed partial class WindowsContentPipelineWorker : IContentPipelineWorker
{
    private readonly Lock lifetimeGate = new();
    private readonly SafeJobHandle job;
    private readonly SafeProcessHandle process;
    private readonly StreamReader outputReader;
    private readonly StreamReader errorReader;
    private bool disposed;

    private WindowsContentPipelineWorker(
        SafeJobHandle job,
        SafeProcessHandle process,
        StreamReader outputReader,
        StreamReader errorReader,
        IProgress<ContentPipelineProcessOutput>? output)
    {
        this.job = job;
        this.process = process;
        this.outputReader = outputReader;
        this.errorReader = errorReader;
        this.StandardOutput = ReadOutputAsync(outputReader, isStandardError: false, output);
        this.StandardError = ReadOutputAsync(errorReader, isStandardError: true, output);
        this.Exit = this.WaitForJobExitAsync();
    }

    /// <inheritdoc />
    public Task<int> Exit { get; }

    /// <inheritdoc />
    public Task<string> StandardOutput { get; }

    /// <inheritdoc />
    public Task<string> StandardError { get; }

    /// <inheritdoc />
    public bool Terminate()
    {
        lock (this.lifetimeGate)
        {
            if (this.disposed || this.GetActiveProcessCount() == 0)
            {
                return false;
            }

            if (WindowsWorkerNative.TerminateJobObject(this.job, WindowsWorkerNative.CancelledExitCode))
            {
                return true;
            }

            var error = Marshal.GetLastPInvokeError();
            return this.GetActiveProcessCount() == 0
                ? false
                : throw new Win32Exception(error, "Failed to terminate the owned content worker job.");
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.lifetimeGate)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.outputReader.Dispose();
            this.errorReader.Dispose();
            this.process.Dispose();
            this.job.Dispose();
        }
    }

    /// <summary>Creates a contained worker using its structured launch request.</summary>
    /// <param name="startInfo">The executable, arguments, and working directory.</param>
    /// <param name="output">Optional observer for complete lines, drained even during cancellation.</param>
    /// <returns>The worker owning the job and output readers.</returns>
    internal static IContentPipelineWorker Start(ProcessStartInfo startInfo, IProgress<ContentPipelineProcessOutput>? output = null)
    {
        var commandLine = WindowsWorkerCommandLine.Create(startInfo);
        var ownedJob = CreateConfiguredJob();
        AnonymousPipeServerStream? outputPipe = null;
        AnonymousPipeServerStream? errorPipe = null;
        StreamReader? ownedOutput = null;
        StreamReader? ownedError = null;
        SafeProcessHandle? ownedProcess = null;
        try
        {
            outputPipe = new(PipeDirection.In, HandleInheritability.Inheritable);
            errorPipe = new(PipeDirection.In, HandleInheritability.Inheritable);
            using var input = new AnonymousPipeServerStream(PipeDirection.Out, HandleInheritability.Inheritable);
            ownedOutput = new(outputPipe, Encoding.UTF8, detectEncodingFromByteOrderMarks: true);
            ownedError = new(errorPipe, Encoding.UTF8, detectEncodingFromByteOrderMarks: true);
            ownedProcess = CreateNativeProcess(startInfo, commandLine, ownedJob, input, outputPipe, errorPipe);
            outputPipe.DisposeLocalCopyOfClientHandle();
            errorPipe.DisposeLocalCopyOfClientHandle();
            input.DisposeLocalCopyOfClientHandle();
            var worker = new WindowsContentPipelineWorker(ownedJob, ownedProcess, ownedOutput, ownedError, output);
            ownedJob = null;
            ownedProcess = null;
            ownedOutput = null;
            ownedError = null;
            outputPipe = null;
            errorPipe = null;
            return worker;
        }
        finally
        {
            ownedJob?.Dispose();
            ownedProcess?.Dispose();
            ownedOutput?.Dispose();
            ownedError?.Dispose();
            outputPipe?.Dispose();
            errorPipe?.Dispose();
        }
    }

    private static SafeJobHandle CreateConfiguredJob()
    {
        var ownedJob = WindowsWorkerNative.CreateJobObject(0, 0);
        var limits = new WindowsWorkerNative.ExtendedLimitInformation
        {
            BasicLimitInformation = new() { LimitFlags = WindowsWorkerNative.KillOnJobClose },
        };
        if (!ownedJob.IsInvalid && WindowsWorkerNative.SetInformationJobObject(
            ownedJob, 9, in limits, (uint)Marshal.SizeOf<WindowsWorkerNative.ExtendedLimitInformation>()))
        {
            return ownedJob;
        }

        var error = Marshal.GetLastPInvokeError();
        ownedJob.Dispose();
        throw new Win32Exception(error);
    }

    private static SafeProcessHandle CreateNativeProcess(
        ProcessStartInfo startInfo,
        char[] commandLine,
        SafeJobHandle ownedJob,
        AnonymousPipeServerStream input,
        AnonymousPipeServerStream output,
        AnonymousPipeServerStream error)
    {
        using var attributes = new WindowsWorkerAttributes(
            ownedJob.DangerousGetHandle(),
            input.ClientSafePipeHandle.DangerousGetHandle(),
            output.ClientSafePipeHandle.DangerousGetHandle(),
            error.ClientSafePipeHandle.DangerousGetHandle());
        var startup = new WindowsWorkerNative.StartupInfoEx
        {
            StartupInfo = new()
            {
                Size = (uint)Marshal.SizeOf<WindowsWorkerNative.StartupInfoEx>(),
                Flags = WindowsWorkerNative.UseStandardHandles,
                StandardInput = input.ClientSafePipeHandle.DangerousGetHandle(),
                StandardOutput = output.ClientSafePipeHandle.DangerousGetHandle(),
                StandardError = error.ClientSafePipeHandle.DangerousGetHandle(),
            },
            AttributeList = attributes.Pointer,
        };

        // JOB_LIST contains the child before its first instruction; HANDLE_LIST
        // limits inheritance to the three operation-owned standard streams.
        if (!WindowsWorkerNative.CreateProcess(
            startInfo.FileName,
            commandLine,
            0,
            0,
            inheritHandles: true,
            WindowsWorkerNative.CreateNoWindow | WindowsWorkerNative.ExtendedStartupInfoPresent,
            0,
            startInfo.WorkingDirectory,
            ref startup,
            out var information))
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }

        using var thread = new SafeWaitHandle(information.Thread, ownsHandle: true);
        return new SafeProcessHandle(information.Process, ownsHandle: true);
    }

    private async Task<int> WaitForJobExitAsync()
    {
        // ActiveProcesses covers descendants even after the root worker exits.
        // A completion-port message is not required to prove the job is empty.
        while (this.GetActiveProcessCount() != 0)
        {
            await Task.Delay(20, CancellationToken.None).ConfigureAwait(false);
        }

        return WindowsWorkerNative.GetExitCodeProcess(this.process, out var exitCode)
            ? unchecked((int)exitCode)
            : throw new Win32Exception(Marshal.GetLastPInvokeError());
    }

    private uint GetActiveProcessCount()
        => WindowsWorkerNative.QueryInformationJobObject(
            this.job,
            1,
            out var accounting,
            (uint)Marshal.SizeOf<WindowsWorkerNative.BasicAccountingInformation>(),
            0)
            ? accounting.ActiveProcesses
            : throw new Win32Exception(Marshal.GetLastPInvokeError());
}
