# 14 — Can we replicate RPCS3's save decryption?

**Short answer: there is nothing to replicate. RPCS3's save code contains no decryption at all.**

RPCS3 reads and writes savedata as **opaque bytes** and merely *logs* secure-file IDs. So "RPCS3
decrypts it, let's copy that" is a dead end on inspection — there is no such code to port. Whatever
makes a console save work under RPCS3 happens elsewhere (most plausibly in the guest, via the emulated
firmware layer RPCS3 provides and the port does not — see §1.3, flagged as inference).

AC4-Port, by contrast, defines its save container as **plaintext** ("files are stored unencrypted"),
which is self-consistent for saves it wrote itself but means a console save reaches the game as
ciphertext, which AC4 cannot read.

Sources checked (fetched from RPCS3 master):
`rpcs3/Emu/Cell/Modules/cellSaveData.cpp` (94,848 B), `rpcs3/Crypto/unedat.cpp` (27,278 B),
`rpcs3/Crypto/unedat.h`, `rpcs3/Crypto/aes.cpp`.

---

## 1. What RPCS3 actually does with savedata

### 1.1 The file layer has no crypto at all

`cellSaveData.cpp` reads and writes save files as opaque bytes:

```cpp
// write path (~line 2239)
ensure(fs::write_file(new_path + vfs::escape(pair.first), fs::rewrite, fvec.obj));
```

`pair.first` is a game-supplied filename, `fvec.obj` the game-supplied buffer. There is no
encrypt/decrypt call anywhere in the file's save or load path — the only crypto includes in the tree
are `Crypto/unedat.cpp` (EDAT/SDAT, i.e. *signed content packages*, a different feature) and
`Crypto/aes.cpp` (the primitive those use).

### 1.2 "Secure" files are only *logged*

The game marks individual files protected by asking for `CELL_SAVEDATA_FILETYPE_SECUREFILE`. RPCS3's
entire handling of that case is one log line:

```cpp
if (type == CELL_SAVEDATA_FILETYPE_SECUREFILE)
{
    cellSaveData.notice("SECUREFILE: %s -> %s", file_path, fileSet->secureFileId);
}
```

The `secureFileId` is dropped. Nothing is decrypted, nothing is re-encrypted. RPCS3 also records which
files the game called "secure" as `*<name>` integers in `PARAM.SFO` — bookkeeping only.

### 1.3 Where the decryption must therefore be (inference, not measured)

On real hardware `cellSaveData` is firmware. The guest passes a **secure file ID**; the firmware
encrypts/decrypts with keys the firmware owns, and the game never sees the raw key schedule. RPCS3
reimplements the `cellSaveData` *API* but not that crypto (see above).

So the ciphertext in the user's `save_import` folder has to be turned into plaintext by *something*
between the file and the game's buffer. Under RPCS3 the only candidate is firmware code the guest
executes; under AC4-Port that layer does not exist at all, so nothing converts it.

**Caveat, stated plainly:** I have not run the console save under RPCS3 to observe this, and I have not
found the firmware routine in the modules the port recompiles (§3). The claim "RPCS3 succeeds because
the guest decrypts via emulated firmware" is the most consistent explanation for RPCS3's save code
containing no crypto, but it is an inference. The *actionable* result does not depend on it — see §4.

---

## 2. Why AC4-Port fails where RPCS3 succeeds

`ac4-port/src/runtime/hle/save_data.cpp`, first comment block:

> Saves live in the console layout under /dev_hdd0/home/00000001/savedata/<DIR> …
> **Files are stored unencrypted**, so saves are not interchangeable with a real console's.

That is a deliberate design choice, and it is correct *for saves the port wrote itself*: the port's
`SEARCH`… `SAVE GAME` path stores `GPROG.DAT` = 3624 B all-zero, entropy 0.00. But it means a
console save handed to the port is delivered to the game as ciphertext, and the game — which has no
notion of a plaintext save — treats it as garbage.

The rig's `save_bypass()` patches only the *status*:

```cpp
if (r == CB_ERR_BROKEN && rt::rig::save_bypass()) {
  const_cast<CBResult&>(res).result = CB_OK_LAST;   // let the load list proceed
  return CELL_OK;
}
```

so the menu opens; the profile behind it stays empty. That is the `AP=0` measured in `HANDOFF.md` §0.7.

---

## 3. Evidence the encryption is firmware-side, not in AC4

If the game en/decrypted its own save files, the cipher (or at least its tables) would be in the
binary. It is not:

| scanned | AES sbox | AES Te0 | AES inv-sbox | SHA-256 K |
|---|---|---|---|---|
| `eboot.elf` (17.2 MB) | 0 | 0 | 0 | 0 |
| the 10 LLE firmware modules in `tools\work\modules93` | 0 | 0 | 0 | 0 |

Both plain and byte-swapped table layouts were tried. So neither the game nor the recompiled firmware
modules contain an AES implementation. **Important caveat:** the port only ships **10** LLE modules
(`libsre`, `libsail`, `libadec`, `libat3dec`, `libatrac3plus`, `libatxdec`, `libdmux`, `libdmuxpamf`,
`libpamf`, `libvpost`) — the ones its media/system paths needed. A real 4.93 firmware has hundreds,
so the AES implementation is very likely present in a module the port never imports (e.g. a
`libsysutil`/`libfs` savedata component). The correct statement is therefore: **the crypto is not in
anything the port currently runs**, not "it exists nowhere".

### Cipher shape, from the files themselves

| file | console ciphertext | port plaintext | note |
|---|---|---|---|
| `GPROG.DAT` | 3632 | 3624 | 227×16 |
| `ACDES.DAT` | 7856 | 7856 | 491×16 |
| `OSET.DAT` | 64 | 62 | 4×16 |
| `PDATA.DAT` | 80 | 76 | 5×16 |

Every ciphertext is an exact multiple of 16 with zero header bytes, and the padding is always
`pad_to_16`. Zero-length header ⇒ the IV is implicit (fixed or key-derived), not stored per file.
Distinct-block counts equal total-block counts in all four files (`OSET.DAT` is 62 bytes of
mostly-zeros yet has **no** repeated block), so this is **CBC or CTR, not ECB**.

That is as far as analysis goes without the key: identifying "AES-CBC/CTR under an opaque
system-firmware key" does not let you decrypt.

### One loose end worth recording

`PARAM.SFO` carries **no** `*FILE` secure flags for this save, yet the files are plainly encrypted.
So the protection here is not the game's optional secure-file marking — it is unconditional on the
console side. `PARAM.PFD` (32,768 B, magic `00 00 00 00 50 46 44 42` = `\0PFDB`, 1835 non-zero bytes) is
the firmware's integrity/HMAC database; RPCS3 reserves a constant 35 KB for SFO+PFD but never validates
it. Worth checking whether AC4's own load path ever verifies PFD after decryption — if it does, a
decrypted-but-unre-signed save would be rejected, which would matter for any future route.

---

## 4. Verdict and options

**We cannot replicate RPCS3's decryption, because there is no decryption in RPCS3 to replicate.** Copying
RPCS3's save code reproduces a pass-through; the game would still receive ciphertext.

The realistic options, cheapest first:

1. **Make a profile with the port itself (recommended).** The port's container is already correct for
   saves *it* writes. Starting NEW GAME gives a valid AC, and the tutorial is proven to progress
   (`m710 → m711`). Save from the hub (SYSTEM → SAVE GAME), and from then on the save is plaintext,
   writable, resumable and editable. This unblocks the `AP=0` symptom for all mission work with no
   port changes.
2. **Obtain a profile that is already plaintext** — e.g. one exported from an RPCS3 profile made by
   playing NEW GAME under RPCS3. Note the port's blank save is 3,624 B of zeros with entropy 0.00,
   while the console `GPROG.DAT` is 3,632 B at entropy 7.96, so the *container* here is definitely
   plaintext and any working profile will look like the port's own.
3. **Implement the firmware secure layer faithfully** (i.e. do what RPCS3 does *not*): learn the
   `cellSaveData` secure-file protocol and its keys, and decrypt/encrypt guest-side. This means pinning
   down the exact key the firmware uses for `BLUS30027`. No public key material for this title was
   found; the standard PS3 savedata keys are not sufficient because the ciphertext here has no PFD-tracked
   secure flags and no per-file header. Large effort, uncertain payoff, and it only restores *reading*
   someone else's save.

**Recommendation: option 1.** It costs one playthrough of the tutorial and makes the mission-development
loop self-sustaining. Option 3 is the only way to use the existing console save, and it is a research
project in itself with no proven key.

### 4.1 Empirical check before committing to option 3

If option 3 is ever attempted, the first question is *which* firmware routine does the work. A cheap
experiment answers it: run the console save under RPCS3 with `--decrypt`-style logging / a debugger,
or simply observe whether RPCS3's `dev_hdd0/.../savedata/...` copy **stays ciphertext** after the game
loads it. If it stays ciphertext, the guest decrypted in memory via an emulated firmware HLE/LLE
routine — and that routine, plus its key, is the thing to port. If it becomes plaintext on disk,
RPCS3 rewrote it and the answer is in the guest's own save path instead. Either result is one run.

---

## 5. Note for the write-up

This is a useful negative result for the feasibility report. Two things are true and worth separating:

- **The port's plaintext container is a deliberate scope choice**, documented at the top of
  `save_data.cpp`, and it is correct for saves the port writes itself. It is not a bug.
- **"RPCS3 can read console saves, so the port can too" is not supported by reading RPCS3's code.**
  Its savedata module contains no crypto; the work must happen in firmware the port does not emulate.
  So the gap is "missing firmware secure layer", not "port a function".

The practical consequence for planning: do not budget a task called "port RPCS3's save decryption" — it
does not exist. Budget either "make a profile in-game" (cheap, recommended) or "faithfully implement the
`cellSaveData` secure-file layer including key acquisition" (research project, unproven).
