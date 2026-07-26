# Crash reports

## For reporters

When the game crashes with the SRT loaded, the plugin writes three files into the **game's
working directory** (next to `re9.exe`):

| File | What it is |
|---|---|
| `SRTPluginRE9_<timestamp>UTC.dmp` | The minidump. Large, but the only artifact that can be debugged properly. |
| `SRTPluginRE9_<timestamp>UTC.crash.txt` | Plain-text summary. Small enough to paste straight into an issue. |
| `SRTPluginRE9.log` | The running log, with the crash appended at the end. |

Please attach all three. The `.crash.txt` alone is often enough to triage.

### If the dump is too big to upload

Open `SRTRE9_ImGui.ini` and set:

```ini
[SRTSettings][General]
CrashDumpIncludeCodeSegments=0
```

That drops the game's code segments from the dump. It shrinks the file a lot, and the
faulting instruction is still captured — you lose the ability to disassemble the wider
surrounding code, not the crash site itself.

### Getting a better dump than the plugin can make

The plugin dumps itself from inside the crashing process, which is inherently limited. If a
crash reproduces, an out-of-process full dump from Windows itself is strictly better and
needs no special build:

```bash
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\re9.exe" /v DumpType /t REG_DWORD /d 2 /f
```

Dumps then land in `%LOCALAPPDATA%\CrashDumps`. These are multi-gigabyte — zip before
sharing. Remove the key afterwards.

---

## For analysts

### 1. Get the matching PDB

**A local rebuild of the same commit will not work.** MSVC mints a fresh PDB GUID on every
link, so a rebuild produces a binary the debugger refuses to match even when the source is
byte-identical. You need the PDB from the exact build the reporter ran.

Two ways:

- **Symbol server** (preferred). Every release publishes its PDBs into an indexed symbol
  store. Set it once:

  ```bash
  setx _NT_SYMBOL_PATH "srv*C:\symbols*https://msdl.microsoft.com/download/symbols;srv*C:\symbols*<SRT symbol store URL>"
  ```

- **Release zip.** Line 1 of `SRTPluginRE9.log` gives the version, e.g.
  `RE9 SRT: v0.3.0+38.aa849aa`. Download that release's zip and point `.sympath` at the
  `SRTPluginRE9.pdb` inside it.

Line 2 of the log records the build's PDB GUID, age and link timestamp directly, so you can
confirm a match before loading anything:

```
Build identity: PDB {7CA610B5-4A31-4968-A7EE-0F61B058EE1F} age 13  (symbol key 7CA610B54A314968A7EE0F61B058EE1FD), link timestamp 0x6A657C86, ...
```

Both forms are printed on purpose. The braced GUID and decimal age are what
`llvm-pdbutil dump --summary` and most tooling show. The **symbol key** is the same GUID with
the age appended **in hex and with no separator** — that is what names the directory in a
symbol store and what debuggers match on. Watch out for the join: an age of 13 becomes a
trailing `D`, so the key can look like a GUID with a stray character on the end. The GUID is
always exactly the first 32 characters; everything after that is the age.

### 2. Work the dump

```
.exr -1
.ecxr
k
lm
!address @rip
u @rip
```

`.ecxr` is not optional. Without it the debugger shows the thread's *current* context, which
is `dbgcore!MiniDumpWriteDump` running inside our own exception filter — not the crash. That
alone accounts for a lot of confusing `unknown!unknown` output.

The recent log is embedded in the dump as a comment stream; WinDbg prints it when the dump
loads, and `.dumpdebug` lists every stream present.

### 3. Understand what the dump can and cannot tell you

The plugin's `SetUnhandledExceptionFilter` is **process-wide**. It fires for *any* crash in
`re9.exe`, including crashes that have nothing to do with the SRT. A dump existing is not
evidence the plugin caused anything. Check whether SRT frames actually appear on the faulting
stack before drawing conclusions.

`re9.exe` has no public symbols — its CodeView record points at `re9_security.pdb`, which
Capcom does not publish — and the shipping image is packed, so the on-disk bytes do not match
what runs at a given RVA. This is why the dump captures in-memory code: it is the only way to
disassemble a fault inside the game.

### Dump contents and what each flag buys

`CrashHandler.cpp` requests these; each one maps to a debugger capability that the old
`MiniDumpNormal` dumps did not have:

| Flag | Capability |
|---|---|
| `MiniDumpWithCodeSegs` | `u` / `ub` — disassemble the faulting instruction |
| `MiniDumpWithDataSegs` | Read globals |
| `MiniDumpWithIndirectlyReferencedMemory` | Follow pointers held in registers and on the stack |
| `MiniDumpWithFullMemoryInfo` | `!address` |
| `MiniDumpWithHandleData` | `!handle` |
| `MiniDumpWithThreadInfo` | Thread start addresses and CPU times |
| `MiniDumpWithUnloadedModules` | Resolve IPs into DLLs that have since unloaded |
| `MiniDumpWithModuleHeaders` | PE section layout without needing the image on disk |
| `MiniDumpIgnoreInaccessibleMemory` | One bad page no longer fails the whole dump |

A `MINIDUMP_CALLBACK` keeps this affordable. Code and data segments are written only for
`re9.exe`, `SRTPluginRE9.dll`, and REFramework; the ~180 other modules keep their module
record and CodeView record (so symbols still bind) but contribute no bulk. A second callback
adds targeted windows around `RIP`, every general-purpose register, and the whole plugin
image — so the fault site survives even with code segments switched off.

---

## Testing the crash handler

Set `DebugEnable=1` in `SRTRE9_ImGui.ini` and press **F9** in game. That raises a read from
`-1` on the game's message thread — the same shape as real reports — and exercises the whole
path. Confirm afterwards that both the `.dmp` and `.crash.txt` were written and that the log
gained a `Crash report:` line.
