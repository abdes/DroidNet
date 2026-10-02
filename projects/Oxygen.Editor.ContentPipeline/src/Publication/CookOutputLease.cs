// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns short publication selection gates and whole-operation lifetimes across processes.</summary>
internal static class CookOutputLease
{
    internal static async Task<CookOutputWriteLease> AcquireWriteAsync(string projectRoot, CancellationToken cancellationToken)
    {
        var (root, cook) = PreparePaths(projectRoot);
        var path = Path.Combine(cook, "publication.lock");
        RejectReparsePoint(path);
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var handle = WindowsCookFile.TryOpenExclusive(path, FileMode.OpenOrCreate, out _);
            if (handle is not null)
            {
                return new(root, handle);
            }

            await Task.Delay(50, cancellationToken).ConfigureAwait(false);
        }
    }

    internal static FileStream AcquireOperation(string projectRoot, Guid operationId)
        => TryAcquireOperation(projectRoot, operationId)
            ?? throw new CookOutputBusyException("The cook operation still owns work.");

    internal static FileStream? TryAcquireOperation(string projectRoot, Guid operationId)
        => TryAcquireOperationFile(projectRoot, operationId, "operation.lock");

    internal static FileStream? TryAcquireRetryInput(string projectRoot, Guid operationId)
        => TryAcquireOperationFile(projectRoot, operationId, "retry-input.lock");

    private static FileStream? TryAcquireOperationFile(string projectRoot, Guid operationId, string name)
    {
        if (operationId == Guid.Empty)
        {
            throw new ArgumentException("An operation identity is required.", nameof(operationId));
        }

        var (_, cook) = PreparePaths(projectRoot);
        var directory = Path.Combine(cook, operationId.ToString("N"));
        RejectReparsePoint(directory);
        _ = Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, name);
        RejectReparsePoint(path);
        return WindowsCookFile.TryOpenOperation(path, out _);
    }

    internal static void RejectReparsePoint(string path)
    {
        var attributes = new FileInfo(path).Attributes;
        if (attributes != (FileAttributes)(-1) && attributes.HasFlag(FileAttributes.ReparsePoint))
        {
            throw new IOException($"Cook ownership paths must not redirect outside the project: '{path}'.");
        }
    }

    private static (string root, string cook) PreparePaths(string projectRoot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        var root = Path.GetFullPath(projectRoot);
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
        return (root, cook);
    }
}
