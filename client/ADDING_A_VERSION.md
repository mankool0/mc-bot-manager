# Adding a New Minecraft Version

Checklist for onboarding a new Minecraft version to the client mod. Work through it top
to bottom; most steps are small.

## 1. Overview

The client builds against exactly one Minecraft version at a time, selected by
`minecraft_version`. Version-specific code lives in per-version compat dirs under
`src/compat/api_*/`. One compat dir covers one *API surface*, not necessarily one
version - multiple Minecraft versions may share a compat dir if the APIs the mod touches
are identical. Only add a new dir when the existing ones no longer compile or behave
correctly against the new version.

## 2. Prerequisites

- A JDK matching the new version's requirement (see `javaVersion` in existing
  `mcVersions` entries; 1.21.x uses 21, 26.1.x uses 25).
- Meteor Client and Baritone SNAPSHOT builds published for the target version:
  - Meteor: https://maven.meteordev.org/snapshots (`meteordevelopment:meteor-client`)
  - Baritone: same maven (`meteordevelopment:baritone`)
- A Fabric Loader + Fabric API release for the target version: https://fabricmc.net/develop/

## 3. `gradle.properties`

`minecraft_version` selects the default build target. Any version can be built one-off
without editing the file: `./gradlew build -Pminecraft_version=<v>`.

## 4. `build.gradle`: `mcVersions` entry

Add a row to the `mcVersions` map at the top of `build.gradle`:

```groovy
'<mc version>': [loader: '<fabric loader>', fabric: '<fabric api>', meteor: '<meteor snapshot>', baritone: '<baritone snapshot>', compat: 'src/compat/api_<x>/java', compatResources: 'src/compat/api_<x>/resources', javaVersion: <jdk>],
```

Version strings: loader and fabric from https://fabricmc.net/develop/, meteor and
baritone from the Meteor snapshots maven (browse for the newest `<version>-SNAPSHOT`).
An unknown `minecraft_version` fails fast with the list of valid keys.

## 5. `settings.gradle`: obfuscation auto-detection

No action needed. Versions not matching `1.*` (Mojang's new `26.x`+ scheme) are
unobfuscated; `settings.gradle` automatically sets `fabric.loom.disableObfuscation` for
them via `gradle.beforeProject`. Only revisit this if Mojang changes versioning scheme
again. For unobfuscated versions there are no mappings and dependencies use plain
`implementation` instead of `modImplementation` (already handled in `build.gradle`).

## 6. Creating a compat dir

Only if the new version cannot share an existing dir:

1. Copy the nearest existing dir, e.g.
   `cp -r src/compat/api_26_1 src/compat/api_<x>` (delete `resources/` contents unless
   you actually need an override, see section 8).
2. Port every method body in
   `src/compat/api_<x>/java/mankool/mcBotClient/util/VersionCompat.java` to the new
   version's API. Use the decompiled reference sources (section 10) to find renames.
3. Never change a method signature in only one dir. The signature set must stay
   identical across all compat dirs; if a new version forces a signature change, apply
   it to every `VersionCompat.java` and all call sites in `src/main/java`.

## 7. Parity check

`./gradlew checkCompatParity` (also runs automatically before every `compileJava`)
verifies that all `VersionCompat.java` files declare identical `public static`
signatures. On drift it fails listing, per dir, which signatures are missing. A
signature missing from every dir except one means that one dir added or renamed a
method. Note the comparison includes parameter names - keep them identical too.

## 8. Per-version resource overrides

The compat `resources/` dir is listed *before* `src/main/resources`, and duplicate
handling is first-seen-wins (`DuplicatesStrategy.EXCLUDE`). A file in the compat
resources dir therefore replaces the main copy of the same path in the built jar. Only
add an override when the file must actually differ for that version.

## 9. Mixins retargeting

Verify every mixin listed in `src/main/resources/mc-bot-client.mixins.json` still
applies on the new version (injection targets can be renamed or removed between
versions). Mixin failures surface at client launch as apply errors in the log. The
mixin classes live in `src/main/java/mankool/mcBotClient/mixin/`.

## 10. Reference sources

Add the new version to the `VERSIONS` array in `refSource/setup-ref-sources.sh` and run
the script. It decompiles Minecraft into `refSource/minecraft-<version>/` (gitignored)
for API reference while porting `VersionCompat` and mixins.

## 11. Manager-side data-version thresholds

If the new version changed the world save format, the manager needs a new threshold in
`manager/world/NBTSerializer.h` (see `DATA_VERSION_1_21_5 = 4325`,
`DATA_VERSION_26_1 = 4786`) plus corresponding handling in the world-saving code
(`WorldExporter`, `ChunkSavingWorker`, `WorldAutoSaver`). Find the new data version at
https://minecraft.wiki/w/Data_version.

## 12. Version lockstep with the manager

`mod_version` in `client/gradle.properties` and the `project(... VERSION ...)` in
`manager/CMakeLists.txt` must match - the manager rejects a mod whose version differs
at handshake. Adding a Minecraft version does not itself require a bump, but any
release shipping the new support bumps both together.

## 13. Build verification

```bash
./gradlew checkCompatParity
./gradlew clean build -Pminecraft_version=<each supported version>
```

CI's release matrix (`.github/workflows/release.yml`) builds every version in
`mcVersions`; keep its `mc_version` matrix in sync with the map.

## 14. In-game test checklist

On the new version, via the manager:

- Connect to a server; handshake accepted, bot appears online
- Inventory sync (open inventory, items visible in manager)
- Container interaction (open chest, click/shift-click slots from a script)
- World saving (chunks + block entities export, level.dat loads in vanilla)
- Meteor module control (enable/disable a module from the manager)
- Baritone (goto command paths correctly, baritone log events arrive)
