# Pak signing

Status: proposal

Last updated: 2026-10-31

A signed pak proves who built it and that its bytes are the ones they built.
The runtime checks it with a public key, so no secret ships with the game.
Signing protects integrity and authenticity, not confidentiality: the engine
does not encrypt paks.

## Contents

- [Scenarios](#scenarios)
- [What a signature covers](#what-a-signature-covers)
- [Modules](#modules)
- [Format](#format)
- [Keys and trust](#keys-and-trust)
- [Verification](#verification)
- [Building](#building)
- [Dependency](#dependency)
- [Tests](#tests)
- [Order](#order)
- [Open questions](#open-questions)
- [Decisions](#decisions)

## Scenarios

| Id  | Scenario                                                                                                      |
| --- | ------------------------------------------------------------------------------------------------------------- |
| G1  | A shipped game refuses a pak that was modified after it was built, or built by anyone but its publisher       |
| G2  | A multiplayer game knows every client runs the content it shipped, without hashing the whole install at start |
| G3  | A library pak (the engine's, a vendor's) is trusted by its own key, not by the game's                         |
| G4  | A development build mounts unsigned paks freely, and reports what it would refuse                             |

## What a signature covers

The catalog digest identifies a pak's content: its library, entries,
descriptor and resource digests, deletions, bases and dependencies. It does
not cover the bytes as stored, which compression and alignment change, so a
signature over the catalog alone would accept a pak whose stored bytes were
replaced. A signature therefore covers a table of the pak's stored regions:

- **The region table** lists every byte range of the pak except the footer
  and the signature section itself, each with the SHA-256 of its stored
  bytes: the header, the directory, the browse index, the catalog, each
  resource table, each storage table, and each resource's stored data.
- **The signed message** is a domain tag (`oxygen.pak.signature.v1`), the pak's
  source key and content version, its catalog digest, and the SHA-256 of the
  region table.
- **Lazy checking follows.** A reader verifies the signature once at mount,
  over the small table; then each region's hash is checked when the region is
  read, by Serio's `VerifiedRead` as the bytes pass. A mount reads no payload to
  verify it, and a load verifies exactly what it reads.

## Modules

Each part lives where its knowledge lives
([Serio's place among the modules](../../../src/Oxygen/Serio/Docs/architecture.md#place-among-the-modules)):

| Module                                | Holds                                                                                                                             |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| **Crypto** (`src/Oxygen/Crypto`, new) | Ed25519 key pairs, signing and verification over spans, key ids. No IO, no pak knowledge; depends on Base and the Ed25519 library |
| **Data**                              | The signature section and its region table, their encoding and decoding, the signed message                                       |
| **Cooker** (PakTool)                  | Building the region table from the writer's hashes and signing it with the build machine's key                                    |
| **Content**                           | The trust list, the verification policy, checking the signature at mount, and checking each region as it is read                  |
| **Player**                            | Reporting a refused pak at launch                                                                                                 |
| **Serio**                             | `VerifiedRead`: reading a region while comparing its SHA-256 with an expected digest; it knows nothing of keys                    |

`ARCHITECTURE.md`'s module table gains the Crypto module.

## Format

- **The signature section** sits after the catalog, before the footer:

  ```text
  algorithm      uint8    1 = Ed25519
  key_id         32 bytes SHA-256 of the signing public key
  region_count   uint32
  regions        region_count x { uint64 offset, uint64 size, 32-byte SHA-256 }
  signature      64 bytes Ed25519 over the signed message
  ```

- **The footer** locates it (offset and size). An unsigned pak has none. The
  storage section of [pak compression](../pak-compression/README.md) needs
  two locating fields too; the footer's 28 reserved bytes hold neither pair,
  so the footer grows with the pak format version that introduces both.
- **The CRC stays** as the cheap check for accidental damage over the whole
  file; the signature is what proves authorship.

## Keys and trust

- **Ed25519.** 32-byte public keys, 64-byte signatures, fast verification,
  and no parameters to choose wrong.
- **A key per publisher, not per pak.** The engine's key signs `engine.pak`;
  a game's key signs its own paks; a library vendor's key signs its library.
- **A trust list per runtime:** the executable embeds the public keys it
  accepts, each with the libraries it may sign (the engine key for `Engine`,
  the game key for the game's library, a vendor key for that vendor's). A pak
  signed by a trusted key for another library is refused, so one publisher's
  key cannot stand in for another's content.
- **Private keys never enter the repository.** PakTool reads the signing key
  from a file or an environment variable that the build machine provides.

## Verification

- **Policy, per runtime:** `kRequired` (shipping: an unsigned or invalid pak is
  refused), `kReport` (development: mounted, each failure logged with its
  code), `kOff`.
- **At mount:** the signature section is read and checked against the trust
  list. Failures are typed, with codes: `pak.signature.missing`,
  `pak.signature.untrusted_key`, `pak.signature.library_not_allowed`,
  `pak.signature.invalid`.
- **At read:** a region whose hash differs from the table is
  `pak.signature.region_mismatch`; under `kRequired` the read fails and the
  asset reports it, under `kReport` it is logged.
- **The Player** refuses a pak at launch as it refuses an unmet dependency,
  with `player.launch.signature_invalid`.
- **Patch paks** are signed and checked like any pak; their base check is
  unchanged.

## Building

- PakTool gains `--sign-key <file>` (or the key from an environment
  variable) and signs as its last step: the region hashes come from the
  hashing stream the writer already runs, so signing reads nothing again.
- The build report records the key id and the region count.
- PakDump prints the signature section and, given a trust list, the result.

## Dependency

Ed25519 comes from a maintained library rather than our own code: libsodium
(Conan Center) or Monocypher (small, public domain). Only the Crypto module
links it. The first step confirms the choice and its recipe.

## Tests

- **Sign and verify:** a signed pak verifies; changing any byte of any region
  fails the region check where the region is read; changing the region table
  or the catalog fails the mount check.
- **Trust:** an untrusted key, and a trusted key signing another library, are
  refused with their codes.
- **Policy:** `kRequired` refuses, `kReport` mounts and logs, `kOff` skips.
- **Lazy checking:** mounting a signed pak reads no resource data; loading an
  asset checks only the regions it reads.
- **Format:** an unknown algorithm is refused; an unsigned pak has no section.
- **Patches:** a signed patch over a signed base verifies, and its base check
  still applies.

## Order

1. The Ed25519 library, its recipe, and the Crypto module over it, with its
   sign-and-verify tests.
2. The footer change and the signature section format, with the pak format
   version (together with the compression storage section).
3. Signing in PakTool.
4. Mount and read verification in Content, with the policy and the trust
   list; regions are checked with Serio's `VerifiedRead`.
5. Player and PakDump reporting.

## Open questions

- **The Ed25519 library:** libsodium or Monocypher.
- **Where the trust list lives:** compiled into the executable, or a signed
  file next to it that a launcher can update.

## Decisions

- **Sign stored regions, check them lazily.** The signature proves every stored
  byte, and a load checks only what it reads, through the hashing it already
  does.
- **Public keys only at runtime.** Nothing secret ships, so nothing secret can
  be extracted.
- **Keys are scoped to libraries.** A key is trusted for the libraries it may
  sign, not for every pak.
- **No encryption.** Confidentiality on a player's machine cannot be kept from
  its owner; authenticity can be proved.
