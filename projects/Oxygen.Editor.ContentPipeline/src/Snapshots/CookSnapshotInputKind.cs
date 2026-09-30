// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>The source fact retained by a cook snapshot.</summary>
public enum CookSnapshotInputKind
{
    /// <summary>A regular source whose bytes must be copied.</summary>
    File,

    /// <summary>A present path observed without consuming its bytes.</summary>
    Probe,

    /// <summary>A path that did not exist during discovery.</summary>
    Absent,
}
