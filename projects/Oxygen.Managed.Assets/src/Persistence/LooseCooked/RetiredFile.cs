// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>
/// A file an earlier commit listed and the index does not. Readers of those commits may still open it, a writer deletes
/// it once none does, and it may already be gone.
/// </summary>
/// <param name="Kind">The file's role.</param>
/// <param name="RelativePath">The file's path below the cooked root.</param>
/// <param name="RetiredAt">The first commit that does not list the file.</param>
public sealed record RetiredFile(FileKind Kind, string RelativePath, ulong RetiredAt);
