// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Win32.SafeHandles;

namespace Oxygen.Editor.ContentPipeline.Processes;

/// <summary>Owns a Win32 job handle and its kill-on-close containment.</summary>
internal sealed partial class SafeJobHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    /// <summary>Initializes a new instance of the <see cref="SafeJobHandle"/> class.</summary>
    public SafeJobHandle()
        : base(ownsHandle: true)
    {
    }

    /// <inheritdoc />
    protected override bool ReleaseHandle() => WindowsWorkerNative.CloseHandle(this.handle);
}
