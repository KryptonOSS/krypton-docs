# Architecture

## Cryptographic primitives

All primitives come from RustCrypto crates (`aes-gcm`, `argon2`, `hkdf`,
`sha2`, `subtle`) with randomness from the OS CSPRNG (`OsRng`).

| Primitive | Parameters | Used for |
|---|---|---|
| AES-256-GCM | 96-bit nonces, 128-bit tags | every byte of ciphertext |
| Argon2id | default m=64 MiB, t=4, p=4 (stored in-container) | password → KEK |
| HKDF-SHA256 | random 32-byte salt per object | master/subkey separation |
| SHA-256 | — | blob location hashing only (not secret-derived output exposure) |
| `subtle::ConstantTimeEq` | — | comparisons on secret material |

### KDF parameter bounds (`KdfParams`)

Parameters travel inside containers so defaults can evolve. Because they
are attacker-visible input, parsing enforces hard bounds to prevent
denial-of-service:

| Field | Default | Accepted range when reading |
|---|---|---|
| `m_cost_kib` | 65 536 (64 MiB) | 8 MiB … 4 GiB |
| `t_cost` | 4 | 1 … 64 |
| `p_cost` | 4 | 1 … 16 |

A hostile file can therefore force at most ~minutes of derivation work —
never unbounded allocation or infinite loops.

---

## Key hierarchy

Two independent derivations exist, selected per container by a kind byte:

```
Standalone .krf (kind = password):

  password ──Argon2id(argon_salt, params)──► KEK ──HKDF(object_salt)──► content key

Vault objects (kind = hkdf):

  vault.config: password ──Argon2id(salt, kdf)──► KEK ──AES-GCM-wrap──► master key
                                                                     │
        entry blob ──HKDF(master, object_salt, info‖entry_path)──────┤
        manifest   ──HKDF(master, object_salt, info‖"vault-manifest")─┘
```

Properties that follow:

- **Per-object keys.** Every object's content key differs, even under the
  same password/master: salts are fresh random 32-byte values, and the
  HKDF `info` includes a location identity (§5.4).
- **Password change never touches ciphertext.** Only the wrapped copy of
  the master key in `vault.config` is re-encrypted.
- **No offline verifier.** There is no hash to check guesses against;
  every guess costs one full Argon2id run plus GCM verification.

---

## The universal container format

Every object the library produces — `.krf` files, vault blobs, manifests —
is one envelope. Defined in `src/container.rs`; constants are public where
meaningful (`MAGIC`, `ENVELOPE_VERSION`).

### Wire layout

```text
offset  size                     field
────────────────────────────────────────────────────────────────
0       8    "KRYPTON\0"          magic (NUL is a control byte:
                                  cannot occur in plain text)
8       2    u16 LE               envelope version (currently 1)
10      1    kind                 0 = HKDF-from-master
                                  1 = password-derived
11      32   (kind 0)             object_salt
11      76   (kind 1)             argon_salt[32] ‖ object_salt[32]
                                  ‖ KdfParams[12] (three u32 LE)
…       var  chunk records         [len u32 LE][ciphertext‖tag], repeated
…       4    00 00 00 00          end-of-chunks marker (unauthenticated;
                                  safe — see §6.3)
…       4    len u32 LE           trailer length (≤ 1 MiB enforced)
…       len  trailer              AES-GCM ct‖tag of JSON (§5.3)
```

Header sizes: 43 bytes (kind 0) or 87 bytes (kind 1).

### Header authentication strategy

No bytes of the header are MAC'd directly — instead **every header byte
participates in key derivation**. Flipping any bit of magic, version, kind
or salts produces a different content key, and the first GCM tag check
fails. This makes the whole header authenticated *implicitly*, without a
separate signature pass.

Version mismatch fails early with `UnsupportedVersion(v)`; retired magics
(`KRYPTON2\n`, `KRYPTON3\n`) fail with the explicit `RetiredFormat` error.

### Trailer

After the chunk section, one final record carries a JSON document sealed
with the same content key under the reserved nonce `[0xFF; 12]`:

```json
{
  "typ": "file" | "directory" | "manifest",
  "name": "original-filename.txt",   // .krf only, optional
  "size": 12345,                      // total plaintext length
  "chunk_count": 1                    // number of preceding records
}
```

On read, the trailer is cross-checked against observation: `typ` must
equal the caller-requested payload type, `size` must equal decrypted byte
count, `chunk_count` must equal records seen, and **nothing may follow the
trailer**. Truncation, extension, and appended-junk attacks all fail here
even if a naive reader would have accepted the prefix.

### Identity binding

Containers carry a caller-supplied *identity* string that participates in
key derivation (`info = "krypton-container-v1/" ‖ identity`) :

| Object | Identity | Effect |
|---|---|---|
| Vault entry blob | entry path, e.g. `"photos/a.jpg"` | copying one entry's blob over another's slot fails authentication |
| Manifest | `"vault-manifest"` | fixed constant; manifest location is canonical |
| Standalone `.krf` | empty | files are self-contained; nothing to bind |

This closes the *blob-transplant* attack class entirely: an attacker who
swaps two blobs in the vault directory gets `Corrupted` reports from
`verify()` and errors on extract — never silently wrong content.

### Payload types

`PayloadType` has three variants today. Adding new object kinds (future
metadata sidecars, keyslots, recovery kits) means adding a variant with a
new AAD context string — **never** changing existing ones. This is the
core reason the format can evolve without a format zoo.

---

## Chunking, nonces and AAD binding

`src/stream.rs` implements record framing shared by all containers.

### Chunk records

Plaintext streams through in **64 KiB** chunks (`CHUNK_SIZE`). Each becomes
one record: `[len u32 LE][ciphertext ‖ 16-byte GCM tag]`. Lengths outside
`16 ..= 64 KiB + 16` are rejected before allocation (no decompression-bomb
analog possible; there is no compression).

Maximum object size: `2^24 chunks × 64 KiB = 1 TiB` (`MAX_CHUNK_COUNT`
guard prevents nonce-space exhaustion and counter overflow).

### Nonce strategy

| Record type | Nonce | Rationale |
|---|---|---|
| Data chunk *i* | `[0x00 ×8 ‖ i as u64 BE]` | counter-based: collision-free within an object regardless of restarts |
| Trailer | `[0xFF; 12]` | reserved domain, distinct from data |
| vault.config | `[0x01, 0xFF ×11]` | fixed, safe: unique KEK per vault, message written once ever |
| Manifest/config wraps | fixed nonce + fresh random content key each write | key rotation substitutes for nonce uniqueness |

(The chunk nonce is `n[4..] = index.to_be_bytes()` — eight zero prefix,
eight-byte big-endian counter; the trailer and config nonces are chosen to
never collide with any possible counter value.)

Counter nonces eliminate the birthday-bound reuse risk that plagues
random-nonce-per-chunk designs (the pre-0.3 weakness). Combined with
per-object salts they give every (object, position) pair a unique
(key, nonce) tuple.

### AAD matrix

Every GCM operation binds context. Nothing is ever decrypted "bare":

| Operation | AAD |
|---|---|
| Data chunk *i* | `"krypton-container-v1/{file\|directory\|manifest}"` ‖ index bytes |
| Trailer | chunk context ‖ index ‖ `"/trailer"` |
| vault.config wrap | `"krypton-vault-config-v2"` |

Consequences:

- Reordering two chunks → tag failure.
- Moving a record between positions → tag failure.
- Moving records between containers of different payload types → tag
  failure (context differs).
- Transplanting whole containers between logical locations → key
  derivation differs (§5.4).
- The end-of-chunks marker is *unauthenticated but harmless*: it cannot be
  forged into extra data because `chunk_count` and `size` in the
  authenticated trailer must match observation exactly.

---

## Single-file mode (.krf)

`encrypt_file(password, input, output)`:

1. Generates fresh `argon_salt` and `object_salt`.
2. Builds a kind-1 container streaming the file (identity = empty),
   storing the original filename in the trailer.
3. Writes atomically: temp file (mode 0600) in the destination directory →
   fsync → rename. On Windows: remove-then-rename.
4. If no output path is given, produces `<sha256(argon_salt ‖ object_salt
   )>.krf` — a random-looking name leaking nothing about content.

`decrypt_file(password, input, output)` authenticates the whole container
while streaming, recovers the original filename from the trailer, sanitizes
it, and returns it. Plaintext is written via temp+rename as well.

Memory use is bounded by one chunk buffer (+tag) regardless of file size;
multi-gigabyte files stream at constant memory.

---

## Vault mode

A vault is a directory:

```
myvault/
├── vault.config          bootstrap JSON (plaintext structure, sealed value)
└── d/
    ├── .manifest.enc     the index, as a kind-0 container
    └── <h2>/<h48>.enc    one blob per entry, as kind-0 containers
```

### vault.config (bootstrap)

A small JSON document — *not* a container, because it must be parseable
before any key exists:

```json
{
  "version": 2,
  "kdf": {"m_cost_kib": 65536, "t_cost": 4, "p_cost": 4},
  "salt": "<base64, ≥8 bytes>",
  "wrapped_master_key": "<base64, master ‖ GCM tag>"
}
```

The master key is generated once at `init`, encrypted under the KEK with
AAD `"krypton-vault-config-v2"` and the fixed config nonce (safe: unique
KEK, single short message). Wrong password and tampered config are
indistinguishable (`Authentication`) by design.

### Blob locations

```
blob_path(entry) = d / sha256(master_key ‖ entry_path)[..2] /
                        sha256(...)[2..50] + ".enc"
```

Paths are deterministic (stable across restarts), keyed by the master key
(so unrelated vaults never share blob names), and leak nothing but entry
count and rough sizes via the tree shape.

### Manifest

The index is a `BTreeMap<String, EntryMetadata>` serialized to JSON
`{"version":2,"files":{...}}`, then stored as a kind-0 container with
payload type `Manifest`. Each `EntryMetadata` records original name,
size, directory flag, and child lists for directories.

Load semantics (hardened after audit finding #1):

| State | Result |
|---|---|
| File absent | empty index (fresh/uninitialized data dir) |
| Present but damaged/truncated/unauthentic | **error** — never silently empty |
| Larger than 64 MiB read cap | rejected before allocation |

A fresh random `object_salt` is generated on every save; the manifest is
rewritten wholesale (temp+rename), never mutated in place.

### Operations

| API | Behavior notes |
|---|---|
| `init(pw)` | creates dir mode 0700, config, empty manifest |
| `unlock` / `lock` | unwraps master into memory / drops it |
| `add(src, name)` | validates target name, rejects collisions & separators; directories recurse, creating placeholder entries; every blob temp+rename |
| `list()` | reads manifest only — no blobs touched, fast |
| `extract(name, dest)` | recreates hierarchy; names sanitized; outputs via temp+rename |
| `remove(name)` | removes subtree: blobs *and* manifest records (no orphans from successful ops) |
| `change_password(old,new)` | verifies old, re-wraps same master; ciphertext untouched |
| `verify(pw)` | decrypts every blob (type-checked), cross-checks manifest presence; returns `IntegrityReport{total, verified, missing[], corrupted[]}` |

Directory removal walks the manifest's own child lists, so removing a
directory removes everything beneath it even if nested names were added
out of order.

### Path-traversal defense

Names recovered from trailers/manifests pass `sanitize.rs`: absolute paths,
`..` components, drive letters and NUL bytes are rejected or rewritten
before joining. An encrypted archive cannot plant files outside the
destination.

---

## Crash safety and durability

Invariants: a crash may lose the *current* operation, never corrupt
*previous* state.

- All persistent writes go through: create temp file in same directory →
  restrict to owner-only → write → `sync_all()` → `rename()` over target →
  `sync_dir()` on the parent directory.
- Containers are immutable once written; updates replace whole files.
  There is no in-place mutation anywhere, which also eliminates torn-header
  states.
- Failure mid-add leaves unreferenced blobs (manifest unchanged) — visible
  to nobody, cleaned by future re-adds to the same path; systematic GC is
  roadmapped (`verify --prune`).

---

## Memory hygiene

- Every key is `Key(Zeroizing<[u8; 32]>)`; derived plaintext buffers that
  contain secrets (master key, manifest JSON, trailer records) use
  `Zeroizing<Vec<u8>>`. Overwritten on drop.
- `Debug` for `Key` prints `Key([REDACTED])`; keys are passed by reference
  and never serialized or logged.
- Chunk buffers are reused; scratch vectors holding secret material are
  zeroizing types.
- Not yet done (by design, `forbid(unsafe_code)`): `mlock`, core-dump
  suppression. Roadmapped behind a tiny audited unsafe boundary.

---

## Error taxonomy

One enum (`Error`, thiserror), `Result<T> = std::result::Result<T, Error>`:

| Variant | Means |
|---|---|
| `Authentication` | wrong password/key **or** any tampering — indistinguishable on purpose |
| `InvalidHeader` | malformed magic/kind/lengths |
| `UnsupportedVersion(u32)` | envelope/schema/config version newer than supported |
| `RetiredFormat` | pre-0.4 file detected (helpful migration hint) |
| `InvalidVault` | vault structure problems (bad config, damaged manifest) |
| `VaultLocked` / `VaultUnlocked` | state-machine misuse |
| `EntryExists` / `EntryNotFound` | name conflicts |
| `InvalidName(String)` | validation failure (preview truncated to 32 chars) |
| `Io(std::io::Error)` | filesystem layer |
| `Encryption` / `KeyDerivation` / `InvalidKdfParams` / `MalformedPayload` | internal/crypto-layer failures |

Deliberate policy: callers cannot distinguish "wrong password" from
"corrupted file" — both are `Authentication`. This avoids turning error
messages into a tamper oracle.

---
