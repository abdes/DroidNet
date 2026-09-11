// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Storage.Native;

/// <summary>The storage boundaries at which deterministic I/O failures can be injected.</summary>
internal enum AtomicWriteStage
{
    /// <summary>The temporary file is open, before its payload is written.</summary>
    BeforeWrite,

    /// <summary>The complete payload is written, before flushing it.</summary>
    BeforeFlush,

    /// <summary>The temporary file is flushed and closed, before replacement.</summary>
    BeforeReplace,
}
