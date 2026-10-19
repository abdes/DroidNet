// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Translates a publication's roots into the runtime mount set, in the publication's precedence order.</summary>
internal static class RuntimeBindings
{
    /// <summary>Gets the runtime mounts of a publication: its mountable roots' paths and how the engine reads each one.</summary>
    /// <param name="publication">The captured publication.</param>
    /// <returns>The mounts, lowest precedence first.</returns>
    public static RuntimeCookedRoot[] Of(CookPublicationReadLease publication)
        => [.. publication.MountableRoots.Select(static entry => new RuntimeCookedRoot(entry.Path, KindOf(entry.Root.Container)))];

    private static RuntimeCookedRootKind KindOf(CookPublicationRootContainer container) => container switch
    {
        CookPublicationRootContainer.LooseFolder => RuntimeCookedRootKind.LooseFolder,
        CookPublicationRootContainer.PakFile => RuntimeCookedRootKind.PakFile,
        _ => throw new InvalidDataException("A publication root names an unknown container."),
    };
}
