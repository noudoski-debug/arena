# Source provenance

- Dear ImGui core 1.91.6, OpenGL backend and embedded Roboto font were supplied in
  `@dearvision.zip`. ImGui is by Omar Cornut and contributors (MIT; see licenses/ImGui.txt).
- Dobby ARM64 static archive and header were supplied in `@dearvision.zip`.
  Dobby upstream is jmpews/Dobby (Apache-2.0; see licenses/Dobby.txt).
  The exact build revision/options of the supplied archive are unknown.
- `jni/include/encore/LitvinMenu.h`, `HitEffect.h`, `Vars.h` and `Chams.cpp`/`Chams.h` are adapted from
  the user-supplied `EncoreDLC3_ESP_10FPS_source_fix.zip` (the exact archive and source mapping are recorded in SOURCE_MAP.json). Original credits/comments are retained;
  no additional license for these files was provided in that archive.
- The original DearVision archive attributes its source to xdl_open / @dearvision.
- Internal adapters, offset tooling, root loader and tests were added in this project.

This notice is not a claim that third-party or user-provided components are
relicensed under a common project license. Verify redistribution rights for
all supplied assets before distributing a compiled package.
