// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns short publication selection gates and whole-operation lifetimes across processes.</summary>
internal static class CookOutputLease
{
    internal static async Task<CookOutputWriteLease> AcquireWriteAsync(FileSystemPath projectRoot, CancellationToken cancellationToken)
    {
        var cook = PrepareCookFolder(projectRoot);
        var path = Path.Combine(cook, "publication.lock");
        RejectReparsePoint(path);
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var handle = WindowsCookFile.TryOpenExclusive(path, FileMode.OpenOrCreate, out _);
            if (handle is not null)
            {
                return new(projectRoot, handle);
            }

            await Task.Delay(50, cancellationToken).ConfigureAwait(false);
        }
    }

    internal static FileStream AcquireOperation(FileSystemPath projectRoot, Guid operationId)
        => TryAcquireOperation(projectRoot, operationId)
            ?? throw new CookOutputBusyException("The cook operation still owns work.");

    internal static FileStream? TryAcquireOperation(FileSystemPath projectRoot, Guid operationId)
        => TryAcquireOperationFile(projectRoot, operationId, "operation.lock");

    internal static FileStream? TryAcquireRetryInput(FileSystemPath projectRoot, Guid operationId)
        => TryAcquireOperationFile(projectRoot, operationId, "retry-input.lock");

    internal static void RejectReparsePoint(string path)
    {
        var attributes = new FileInfo(path).Attributes;
        if (attributes != (FileAttributes)(-1) && attributes.HasFlag(FileAttributes.ReparsePoint))
        {
            throw new IOException($"Cook ownership paths must not redirect outside the project: '{path}'.");
        }
    }

    private static FileStream? TryAcquireOperationFile(FileSystemPath projectRoot, Guid operationId, string name)
    {
        if (operationId == Guid.Empty)
        {
            throw new ArgumentException("An operation identity is required.", nameof(operationId));
        }

        var cook = PrepareCookFolder(projectRoot);
        var directory = Path.Combine(cook, operationId.ToString("N"));
        RejectReparsePoint(directory);
        _ = Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, name);
        RejectReparsePoint(path);
        return WindowsCookFile.TryOpenOperation(path, out _);
    }

    private static string PrepareCookFolder(FileSystemPath projectRoot)
    {
        ArgumentNullException.ThrowIfNull(projectRoot);
        var root = projectRoot.FullPath;
        if (!Directory.Exists(root))
        {
            throw new DirectoryNotFoundException($"The project directory does not exist: '{root}'.");
        }

        RejectReparsePoint(root);
        var build = Path.Combine(root, ".build");
        var cook = Path.Combine(build, "cook");
        RejectReparsePoint(build);
        RejectReparsePoint(cook);
        _ = Directory.CreateDirectory(cook);
        return cook;
    }
}
