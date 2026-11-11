//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

//! One cooked asset descriptor the published index lists.
using LooseCookedAssetRecord = data::loose_cooked::IndexedAsset;

//! One cooked file the published index lists.
using LooseCookedFileRecord = data::loose_cooked::IndexedFile;

//! The size and SHA-256 of a file's bytes, computed by the producer that
//! wrote them while it held them.
struct LooseCookedFileDigest final {
  uint64_t size = 0;
  base::Sha256Digest sha256 {};
};

//! Who produces an index entry with an identity.
/*!
 A source produces its assets. A sidecar completes an asset another job
 produced, such as the scene whose scripts it binds, so what it emits is
 produced by that asset's source.
*/
struct LooseCookedProducer final {
  //! Mount path of the producing source, such as `/Content/Ship/Ship.glb`.
  //! For a sidecar, the producer of the asset it completes when this index
  //! does not list that asset, as when it lives in another cooked root.
  std::string source;
  //! Virtual path of the asset a sidecar completes; empty otherwise. When the
  //! index lists it, its producer produces the entry.
  std::string completes;
};

//! A complete re-cook of one source under one identity.
/*!
 One source may back several identities, as when a model is imported twice
 under different names, so a re-cook covers only the outputs at or under its
 own identity.
*/
struct LooseCookedRecook final {
  //! Mount path of the re-cooked source, such as `/Content/Ship/Ship.glb`.
  std::string source;
  //! Virtual path of the job's identity, such as `/Cooked/Ship/Ship.glb`.
  std::string identity;
  //! Container-relative path of the identity, under which the job's
  //! auxiliary files are.
  std::string identity_relpath;
};

//! An index entry refused because its identity belongs to another source.
/*!
 Raised when another source produces the same virtual path, or one differing
 from it only in case, which a case-insensitive file system holds in one file,
 and when two files of the index have paths differing only in case. The import
 reports it as `import.identity_collision`.
*/
class LooseCookedIdentityCollision final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

//! A commit refused because it would list a physics sidecar and a scene it
//! does not match.
/*!
 A scene and its physics sidecar are one regeneration unit: the sidecar names
 the scene's key and the hash of its node layout
 (`data::ComputeNodeLayoutHash`). A session that re-cooks a scene with other
 nodes, or retires it, also re-cooks or removes the sidecar the root lists for
 it; a scene re-cooked with the same nodes keeps its sidecar, whatever else
 changed, and a sidecar is cooked for the nodes of the scene the commit lists.
 The import reports it as `physics.sidecar.stale`.
*/
class LooseCookedStaleSidecar final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

//! An index entry refused because its identity is in another mount.
/*!
 A cooked root holds one mount, the one its identities name
 (`content::MountRootOf`), so `/Content/A.omat` and `/Other/A.omat` never meet
 in one file. The import reports it as `import.output_mount_mixed`.
*/
class LooseCookedOutputMountMixed final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

//! A cooked root another writer holds: its writer lease is taken.
/*!
 A root has one writer at a time, from the start of its session to the end
 of its last cleanup. The import reports it as `import.cooked_root_busy`,
 before anything is written; whoever queues the cook decides how long to
 wait.
*/
class LooseCookedRootBusy final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

//! Result of writing a loose cooked container.
struct LooseCookedWriteResult final {
  std::filesystem::path cooked_root;
  data::SourceKey source_key {};

  uint16_t content_version = 0;
  //! The commit the published index is.
  uint64_t commit = 0;
  //! The assets of the published index, in key order.
  std::vector<LooseCookedAssetRecord> assets;
  //! The files of the published index, in path order.
  std::vector<LooseCookedFileRecord> files;
  //! Container-relative paths of the files the previous commit listed and the
  //! published one retires, in path order; each is deleted once no reader
  //! holds a commit that lists it.
  std::vector<std::string> removed_files;
};

//! Write a loose cooked container root.
/*!
 This is a small, tooling-oriented helper responsible for:
 - writing asset descriptor files,
 - writing optional resource table/data files,
 - emitting a valid `container.index.bin` matching
   the current oxygen::data::loose_cooked index format.

 Every file it reads, writes or removes goes through its
 `cooked::CookerStorage`; a file it writes, the index included, replaces its
 destination atomically.

 It is designed to be used by importers (FBX/glTF) and other cook pipelines.

 ### Commits

 It writes the root by the [loose root
 publication](../../../../../design/proposals/cooked-sources/root-publication.md)
 protocol. The index it publishes at `Finish` is the root's next commit, and
 nothing else commits anything:

 - It takes the root's writer lease when it is created and holds it until it
   is destroyed (LooseCookedRootBusy when another writer holds it), so it
   reads the latest commit once and no other writer commits before it.
 - It then drops what no commit records, its own or a crashed writer's: a
   data file's bytes past its committed length, and every unlisted file of a
   writer's naming (`data::loose_cooked::IsWriterOutputRelPath`).
 - Every file but a resource data file is published under its content name
   (`data::loose_cooked::ContentRelPath`), stably, before the index names it;
   a commit never changes a file another one lists.
 - A file the commit stops listing is retired: the index lists it with that
   commit, and the writer deletes it once no reader holds an earlier commit
   (`.leases/<commit>.lock`), at the start of a session or after its commit.

 ### Identities

 Every asset and texture descriptor records the source that produced it. A
 source writing an identity it already produced replaces its entry. An
 identity another source produced, or one differing from an existing identity
 only in case, is refused with LooseCookedIdentityCollision, and an identity
 in another mount than the index's with LooseCookedOutputMountMixed. A source
 that goes away has its outputs removed first (RemoveAssetDescriptor,
 RemoveAuxiliaryFile), so a new source can then produce them. A complete
 re-cook of a source retires the outputs it no longer writes
 (RetireUnwrittenOutputs), so the index lists only current outputs.

 ### Resource tables

 Resource tables are append-only, so a cook is safe while readers have the
 root open: entries no asset uses any more stay in their tables. The pak step
 compacts them; a full pak holds only the resources its assets reach.

 @note The on-disk schema is fixed by
   oxygen::data::loose_cooked::IndexHeader and related structs.
*/
class LooseCookedWriter final {
public:
  //! Create a writer targeting `cooked_root`, which it reads, writes and
  //! removes files of through `storage`, holding its writer lease.
  /*!
   @throw LooseCookedRootBusy when another writer holds the root.
   @throw std::runtime_error when the published index cannot be read, or what
     no commit records cannot be dropped.
  */
  OXGN_COOK_API LooseCookedWriter(
    std::filesystem::path cooked_root, cooked::CookerStorage& storage);
  OXGN_COOK_API ~LooseCookedWriter();

  OXYGEN_MAKE_NON_COPYABLE(LooseCookedWriter)
  OXYGEN_DEFAULT_MOVABLE(LooseCookedWriter)

  //! Set the source GUID recorded in the index.
  /*!
   If unset, the implementation may generate a new GUID.
  */
  OXGN_COOK_API auto SetSourceKey(std::optional<data::SourceKey> key) -> void;

  //! Set the cook-defined content version recorded in the index header.
  OXGN_COOK_API auto SetContentVersion(uint16_t version) -> void;

  //! Write an asset descriptor and update its index entry.
  /*!
   @param key Stable identity of the asset.
   @param asset_type Loader dispatch type.
   @param virtual_path Virtual path used by tooling/editors.
   @param descriptor_relpath Container-relative layout path for the
     descriptor file; the file gets its content name.
   @param bytes Descriptor bytes; must match the runtime loader schema.
   @param references The asset's resource and key references.
   @param producer The source that produces the asset.

   Existing index entries are preserved. The entry replaces one its source
   produced before; see the class's identity rules.

   @throw LooseCookedIdentityCollision if another source produces the
     identity, or one differing from it only in case.
   @throw std::runtime_error on IO errors or invalid paths.
  */
  OXGN_COOK_API auto WriteAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer) -> void;

  //! Write an arbitrary file and update its index record.
  /*!
   This is primarily intended for resource table/data files referenced by the
   runtime (buffers/textures).

   @param kind Logical kind of file (see LooseCookedIndexFormat.h).
   @param relpath Container-relative layout path; any file but a resource data
     file gets its content name.
   @param bytes File bytes; a resource data file's must begin with its
     committed bytes, since a data file only grows.

   Existing file records are preserved. A data file is identified by its
   kind, and a root holds it at one path; a table of new bytes replaces the
   record of its kind.

   @throw std::runtime_error on IO errors, invalid paths, a second path for a
     data file, or data file bytes that do not extend the committed ones.
  */
  OXGN_COOK_API auto WriteFile(data::loose_cooked::FileKind kind,
    std::string_view relpath, std::span<const std::byte> bytes) -> void;

  //! Register an externally-written file its producer hashed.
  /*!
   This is used when the file was written directly (e.g., a resource table an
   aggregator wrote) rather than through WriteFile(). The file must already
   exist on disk at the given relpath. Re-registering the same path refreshes
   its metadata; a second path for a kind is refused. Finish measures the file
   again before publishing the index and records `produced` as its digest; it
   never reads the file.

   @param kind The file kind to register; not a resource data file.
   @param relpath Container-relative file path.
   @param produced The digest the producer computed as it wrote the file.
   @throw std::runtime_error if `kind` is a resource data file, or the file
     does not exist or does not have the size its producer hashed.
  */
  OXGN_COOK_API auto RegisterExternalFile(data::loose_cooked::FileKind kind,
    std::string_view relpath, const LooseCookedFileDigest& produced) -> void;

  //! Register an externally-written resource data file.
  /*!
   Every import appends to a data file (`ResourceAppender`), so none of them
   knows the digest of the whole file, and the index records none: each
   resource in it is verified against the SHA-256 of its table row. Finish
   only measures the file again before publishing the index.

   @param kind A resource data file kind, such as `kTexturesData`.
   @param relpath Container-relative file path.
   @throw std::runtime_error if `kind` is not a resource data file, or the
     file does not exist.
  */
  OXGN_COOK_API auto RegisterExternalDataFile(
    data::loose_cooked::FileKind kind, std::string_view relpath) -> void;

  //! Register an externally-written auxiliary file its producer produces.
  /*!
   Re-registering the same path refreshes its record. A re-cook of the
   producer retires the file once it no longer writes it.

   @param relpath Container-relative content name of the file, such as a
     `.obuf` buffer sidecar.
   @param virtual_path The virtual path a reference names the file by, as a
     buffer sidecar's; empty for a file nothing references.
   @param producer The source that produces the file.
   @param produced The digest the producer computed as it wrote the file.
   @throw std::runtime_error if the file does not exist, a path is invalid,
     or the file does not have the size its producer hashed.
  */
  OXGN_COOK_API auto RegisterExternalAuxiliaryFile(std::string_view relpath,
    std::string_view virtual_path, const LooseCookedProducer& producer,
    const LooseCookedFileDigest& produced) -> void;

  //! Register an externally-written texture descriptor under its virtual path.
  /*!
   Runtimes and tools resolve the texture by `virtual_path`; its source
   registering it again refreshes its record.

   @param relpath Container-relative path of the `.otex` descriptor.
   @param virtual_path Canonical virtual path of the descriptor.
   @param producer The source that produces the texture.
   @param produced The digest the producer computed as it wrote the file.
   @throw LooseCookedIdentityCollision if another source produces the
     identity, or one differing from it only in case.
   @throw std::runtime_error if the file does not exist, a path is invalid,
     or the file does not have the size its producer hashed.
  */
  OXGN_COOK_API auto RegisterExternalTextureDescriptor(std::string_view relpath,
    std::string_view virtual_path, const LooseCookedProducer& producer,
    const LooseCookedFileDigest& produced) -> void;

  //! Registers an asset descriptor its producer wrote, validated and hashed.
  /*!
   The producer (an emitter) validated the descriptor bytes and hashed them as
   it wrote them, under their content name; the writer records `produced` and
   only measures the file, which must exist at `descriptor_relpath` and hold
   `produced.size` bytes. `requirements` are what the asset requires of other
   mounts, which the index records with it.

   @throw LooseCookedIdentityCollision if another source produces the
     identity, or one differing from it only in case.
   @throw std::runtime_error if the file does not exist or holds another size,
     or a path is invalid.
  */
  OXGN_COOK_API auto RegisterProducedAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, const LooseCookedFileDigest& produced,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer,
    std::span<const data::loose_cooked::IndexedRequirement> requirements = {})
    -> void;

  //! Registers an asset descriptor that exists in the cooked root but that
  //! nothing in this import produced, such as a re-sealed staged descriptor.
  /*!
   The writer reads the file once, validates it against `asset_type`, `key`
   and `references`, and hashes it.

   @throw LooseCookedIdentityCollision if another source produces the
     identity, or one differing from it only in case.
   @throw std::runtime_error if the file cannot be read, does not validate, or
     a path is invalid.
  */
  OXGN_COOK_API auto RegisterExternalAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer) -> void;

  //! Remove an asset's index entry and its descriptor file.
  /*!
   The removal applies to the index published when `Finish()` runs. The
   descriptor file is retired by that commit, and deleted once no reader
   holds an earlier one.
   Resource table entries the asset used stay in their tables: other assets
   may share them, and the pak step drops the ones no packed asset reaches.

   @param virtual_path Virtual path of the asset to remove.

   @throw std::runtime_error from `Finish()` if no asset has this virtual path,
     or if this writer also writes it.
  */
  OXGN_COOK_API auto RemoveAssetDescriptor(std::string_view virtual_path)
    -> void;

  //! Remove an auxiliary file or texture descriptor record and the file.
  /*!
   @param relpath Container-relative path of the file.

   @throw std::runtime_error from `Finish()` if the index has no auxiliary
     file or texture descriptor at this path, or if this writer also writes
     it.
  */
  OXGN_COOK_API auto RemoveAuxiliaryFile(std::string_view relpath) -> void;

  //! Retire the outputs a complete re-cook no longer writes.
  /*!
   When `Finish()` runs, the index stops listing, and the writer deletes, each
   output the published index records for the re-cooked source that this
   writer does not write: asset and texture descriptors at or under the
   identity, and auxiliary files at or under its relative path. Paths and the
   source compare ignoring case. A physics sidecar keeps its scene's producer
   but comes from a job of its own, so it stays. Resource table entries stay in
   their tables; the pak step drops the ones no packed asset reaches.

   @throw std::runtime_error if the source is not a mount path or the identity
     paths are invalid.
  */
  OXGN_COOK_API auto RetireUnwrittenOutputs(const LooseCookedRecook& recook)
    -> void;

  //! Finalize and write the loose cooked index.
  /*!
   The index is streamed into a publication of the storage
   (`data::loose_cooked::EncodeLooseCookedIndex`), never held whole in memory,
   and replaces the published index when it commits, as the commit after the
   one the writer read. Every entry that commit listed and this writer did
   not change stays. Unlisted files this session wrote are then deleted, and
   retired files no reader needs.

   @return Summary of written assets and file records.

   @throw LooseCookedStaleSidecar if the commit would list a physics sidecar
     and a scene it does not match, where this session wrote either or
     retired the scene.
   @throw std::runtime_error if required metadata is missing or index emission
     fails.
  */
  OXGN_COOK_NDAPI auto Finish() -> LooseCookedWriteResult;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import
