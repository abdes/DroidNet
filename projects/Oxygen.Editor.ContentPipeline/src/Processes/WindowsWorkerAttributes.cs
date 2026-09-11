// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Oxygen.Editor.ContentPipeline.Processes;

/// <summary>Owns startup job/handle attributes until native process creation finishes.</summary>
internal sealed partial class WindowsWorkerAttributes : IDisposable
{
    private readonly List<nint> values = [];
    private readonly bool initialized;

    /// <summary>Initializes a new instance of the <see cref="WindowsWorkerAttributes"/> class.</summary>
    /// <param name="job">The job inherited through atomic startup assignment.</param>
    /// <param name="inheritedHandles">The only handles made available to the child.</param>
    internal WindowsWorkerAttributes(nint job, params nint[] inheritedHandles)
    {
        nuint size = 0;
        _ = WindowsWorkerNative.InitializeProcThreadAttributeList(0, 2, 0, ref size);
        if (size == 0)
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }

        this.Pointer = Marshal.AllocHGlobal(checked((nint)size));
        try
        {
            if (!WindowsWorkerNative.InitializeProcThreadAttributeList(this.Pointer, 2, 0, ref size))
            {
                throw new Win32Exception(Marshal.GetLastPInvokeError());
            }

            this.initialized = true;
            this.Add(WindowsWorkerNative.JobListAttribute, [job]);
            this.Add(WindowsWorkerNative.HandleListAttribute, inheritedHandles);
        }
        catch
        {
            this.Dispose();
            throw;
        }
    }

    /// <summary>Gets the native attribute-list pointer valid until disposal.</summary>
    internal nint Pointer { get; private set; }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.Pointer == 0)
        {
            return;
        }

        if (this.initialized)
        {
            WindowsWorkerNative.DeleteProcThreadAttributeList(this.Pointer);
        }

        foreach (var value in this.values)
        {
            Marshal.FreeHGlobal(value);
        }

        Marshal.FreeHGlobal(this.Pointer);
        this.Pointer = 0;
    }

    private void Add(nuint attribute, nint[] handles)
    {
        var bytes = checked(handles.Length * nint.Size);
        var value = Marshal.AllocHGlobal(bytes);
        this.values.Add(value);
        Marshal.Copy(handles, 0, value, handles.Length);
        if (!WindowsWorkerNative.UpdateProcThreadAttribute(this.Pointer, 0, attribute, value, (nuint)bytes, 0, 0))
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
    }
}
