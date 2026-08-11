# Release packaging

`Build-UEPRelease.ps1` creates commit-addressed release artifacts without
installing the plugin into Unreal Engine or modifying engine source.

## Source release

Run from a clean committed worktree:

```powershell
.\Packaging\Build-UEPRelease.ps1
```

The source zip comes from `git archive`, uses an explicit public allowlist and
excludes build output, binaries, intermediates, bytecode caches and internal
reference material. `manifest.json` records the exact commit, version, entry
count, byte size and SHA-256; `SHA256SUMS.txt` is suitable for release upload
verification. Re-running for the same commit produces the same source archive
content while writing to a new non-destructive result directory.

## Optional Win64 binary package

```powershell
.\Packaging\Build-UEPRelease.ps1 `
    -EngineRoot F:\UnrealEngine `
    -IncludeBinary `
    -TargetPlatforms Win64
```

This invokes UE5.8 AutomationTool `BuildPlugin`, validates its success markers,
and adds its hash and engine/platform identity to the same manifest. BuildPlugin
never receives the repository path: the script extracts the audited source zip
to an isolated staging tree and compiles that exact commit. Both the resulting
package tree and final zip are scanned for repository metadata, internal
reference material, temporary host projects and Python bytecode before the
artifact is accepted. A binary artifact is tied to the exact UE version and
target platform in its filename; it is not a portable cross-version plugin
binary.

Artifacts are written under `.build/Releases/<version>/` and remain untracked.
`-AllowDirty` exists only for developing the packaging script; release evidence
must come from the default clean-worktree path.
