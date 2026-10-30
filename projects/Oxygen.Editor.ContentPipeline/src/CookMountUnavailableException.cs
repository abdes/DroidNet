// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects.Mounts;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// An asset's source lies in a declared source mount that this machine cannot read: the mount is not bound, or its
/// folder is missing. Like a missing folder, it is an I/O failure; <see cref="Code"/> is the mount table's code for the
/// state, which the cook reports.
/// </summary>
public sealed class CookMountUnavailableException : IOException
{
    /// <summary>Initializes a new instance of the <see cref="CookMountUnavailableException"/> class.</summary>
    public CookMountUnavailableException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookMountUnavailableException"/> class.</summary>
    /// <param name="message">Which mount is unavailable and why.</param>
    public CookMountUnavailableException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookMountUnavailableException"/> class.</summary>
    /// <param name="message">Which mount is unavailable and why.</param>
    /// <param name="innerException">The underlying failure.</param>
    public CookMountUnavailableException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookMountUnavailableException"/> class.</summary>
    /// <param name="mount">The unavailable mount.</param>
    public CookMountUnavailableException(ResolvedMount mount)
        : base(MessageOf(mount ?? throw new ArgumentNullException(nameof(mount))))
    {
        this.Code = mount.State == MountState.Unbound ? MountCodes.Unbound : MountCodes.FolderMissing;
    }

    /// <summary>Gets the mount table's code for the mount's state: <see cref="MountCodes.Unbound"/> or
    /// <see cref="MountCodes.FolderMissing"/>.</summary>
    public string Code { get; } = MountCodes.Unbound;

    private static string MessageOf(ResolvedMount mount)
        => mount.State == MountState.Unbound
            ? $"The source mount '{mount.Name}' is not bound on this machine; bind it to its folder to cook its content."
            : $"The folder of the source mount '{mount.Name}' is missing: '{mount.Location}'.";
}
