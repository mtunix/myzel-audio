# Installers: Project Plan

> One installer per OS for the released Myzel Audio plugins: Windows (`.exe`), macOS (`.pkg`),
> Linux (`.run`). Built in CI from a release tag, unsigned, with no paid certificates.

The user downloads one file and sees one row per plugin with what is already on the machine.
They tick what to install or update, tick what to remove, and the installer puts the plugin
formats where hosts look for them. The same file updates or trims an older install. Each OS uses
its own native installer technology, so the user sees a familiar installer and gets the OS's
uninstall path.

Today a release is a per-plugin Linux tarball (`tools/package.sh`) with an `install.sh`, plus
unsigned CI artefacts for Windows and macOS that the user has to copy by hand. The installers
replace that as the release artefact; `tools/package.sh` stays for CI and for anyone who wants a
plain tarball.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Installers | One suite installer per OS, all released plugins inside | One download, one place to see what's installed |
| Windows | Inno Setup 6 (`.exe`) | Free (also for AGPL projects), native component checkboxes, Pascal script for detection and removal, uninstaller and Settings → Apps entries, Restart Manager closes hosts holding a DLL |
| macOS | Flat product archive (`pkgbuild` + `productbuild`, `.pkg`) | Ships with Xcode on the runner; the Customize pane is a checklist; **files an Installer writes carry no quarantine flag**, which is what makes unsigned plugins load (*macOS without a Developer ID*) |
| Linux | Self-extracting `.run` (makeself) with our `setup.sh` | No common native installer across distros; makeself checks the archive's checksum; `setup.sh` grows out of today's `install.sh` |
| Checklist on Linux | zenity, else kdialog, else whiptail, else a numbered terminal prompt | A GUI dialog on a desktop, works over SSH too |
| Install scope | Windows and macOS system-wide (admin prompt). Linux: per-user (default) or system-wide, chosen on the first page or with `--system` | Windows hosts scan `Common Files` reliably; macOS pkg installs are system-wide by convention; Linux users want both |
| Formats | Windows: VST3, CLAP, Standalone. macOS: **AU**, VST3, CLAP, Standalone. Linux: VST3, CLAP, LV2, Standalone | AU is needed for Logic, GarageBand and MainStage; LV2 stays Linux-only (only Ardour and Reaper load it elsewhere) |
| Which plugins | Cerulean, Mulberry, Umber. **Nothing in the installers names a plugin**: everything is generated from `installer/plugins.tsv` | Viridian, Halite and Inkcap join with one line each when they are released (*Extensibility*) |
| Versions | Each plugin keeps its own `project()` version; the installer has a suite version (CalVer `2026.11.0`) in `installer/VERSION` | Plugins release at different paces; the suite version names the download |
| Removal | **Per plugin on every OS**, in the installer itself (a *Remove* group) and from the OS's uninstall path; unticking an install row never removes | Asked for; removal stays an explicit act so a mis-click can't break a session |
| Linux baseline | Ubuntu 24.04 (glibc 2.39) | Starting point; the `.run` checks glibc first and says what it needs. An older base is a later step (*Open*) |
| Signing | None paid. macOS ad-hoc signature (free, required on Apple Silicon); Windows unsigned; SHA-256 sums and GitHub build provenance for every file | No Developer ID, no code-signing certificate (*Unsigned distribution*) |
| Where it's built | A new `.github/workflows/release.yml`, on a `release-*` tag or by hand; standard runners only | Keeps `ci.yml` (per-push checks) separate; release builds use LTO |
| Frozen identifiers | Inno `AppId` GUID `{0AFB3883-05BD-477F-B777-E539AC381E0E}`; Windows receipt key `HKLM\Software\Myzel Audio\<Name>` and per-plugin Apps entry `…\Uninstall\MyzelAudio_<plugin>`; pkg identifiers `com.myzelaudio.pkg.<plugin>.<format>` (format `au`, `vst3`, `clap`, `standalone`), `com.myzelaudio.pkg.<plugin>.remove` and `com.myzelaudio.pkg.support`; Distribution choice ids `install.<plugin>`, `install.<plugin>.<format>`, `remove.<plugin>`; Linux receipt paths. Plugin ids are lower-case letters and digits (`tools/installer.sh` checks) | Updates and removal find earlier installs by them, scripted installs name the choices; same rule as parameter IDs |

**Considered and rejected:**

- **A single cross-platform installer app written in JUCE.** Same Gatekeeper and SmartScreen
  hurdles as the native installers, and it would need its own elevation, uninstall registration
  and receipts on three OSes; the native tools bring those.
- **macOS DMG with drag-to-folder.** No checklist, no detection, and bundles dragged out of a
  quarantined DMG stay quarantined, so hosts refuse the unsigned plugins.
- **Homebrew cask.** Homebrew no longer lets a cask skip Gatekeeper, which is the point here.
- **MSI (WiX).** Feature trees work, but detection, the status display and per-plugin removal
  are more work than Inno's Pascal script, and there is no gain without signing.
- **A three-state row (install / keep / remove).** None of the native checklists has one; two
  groups of plain checkboxes work the same on all three OSes.

## Behaviour (all three OSes)

**Pages:** welcome (suite version, licence: AGPLv3; Linux: per-user or system-wide) → checklist
→ format choice → summary (what will be installed, updated, removed) → install → done (where
things went, "rescan plugins in your host").

**The checklist** has two groups.

*Install or update*: one row per plugin in `plugins.tsv`, with name, one-line blurb, the version
this installer carries, and the status found on the machine:

| Status | Shown as | Ticked by default |
|---|---|---|
| Not installed | `not installed` | Yes |
| Older version installed | `installed 0.1.0 → updates to 0.2.0` | Yes |
| Installed, version unknown (hand copy, old tarball, `MYZEL_COPY_PLUGINS`) | `installed (unknown version) → replaces with 0.2.0` | Yes |
| Same version installed | `installed 0.2.0 (up to date)` | No; ticking reinstalls |
| Newer version installed | `installed 0.3.0 (newer)` | No; ticking asks before downgrading |

*Remove*: one row per plugin found on the machine (also plugins found but no longer in this
installer's list), all unticked. Ticking a remove row unticks and disables that plugin's install
row. The summary page lists removals separately and they need the explicit *Install* / *Next*
click like everything else.

A plugin installed in only some formats counts as installed; the row lists the formats found.
On Linux a plugin found in both scopes is flagged (a host may load either copy) and its remove
row offers both. Remove wins per scope: removing from one scope while installing into the other
moves the plugin. The system-wide part runs first, so a dismissed password prompt changes nothing.

**macOS shows no status text.** Installer's choice titles are static, so each row reads
`<Name> <version carried>` and its description states the tick rule; the default ticks still
follow the table (receipt version first, then `CFBundleShortVersionString`). A newer install is
only unticked, not confirmed: Installer has no per-choice dialog. Unticking a *Remove* row does not
restore the install ticks it cleared.

**Formats** are a second level: one checkbox per format, all ticked, applied to every ticked
install row (Windows: Inno *tasks*; macOS: sub-choices per plugin, the pkg's own structure;
Linux: a second dialog). Removal always removes every format of that plugin.

**Installing a plugin** removes its old bundle first, then copies the new one, so a host that
still has the old binary loaded keeps its mapped copy instead of crashing (the existing
`install.sh` rule). Windows asks to close hosts that hold the DLL (Restart Manager).

**Detection** reads the installer's own receipt first and falls back to the file on disk, so it
also finds copies that no installer put there:

| OS | Receipt | Fallback on disk |
|---|---|---|
| Windows | `HKLM\Software\Myzel Audio\<Name>`: `Version`, `Formats` | File version of `Common Files\VST3\<Name>.vst3\Contents\x86_64-win\<Name>.vst3` (JUCE stamps VERSIONINFO) |
| macOS | pkg receipts `com.myzelaudio.pkg.<plugin>.<format>` | `CFBundleShortVersionString` in each bundle's `Info.plist` (AU, VST3, CLAP, app), read from the Distribution script via `system.files.bundleAtPath` |
| Linux | `~/.local/share/myzel-audio/installed/<plugin>` (per-user; `$XDG_DATA_HOME/myzel-audio/…` when that is set, as are the menu entry and icon), `/usr/local/share/myzel-audio/installed/<plugin>` (system): `id=`, `name=`, `version=`, `suite=`, `formats=` and one `file=` line per installed path | `moduleinfo.json` inside the VST3 bundle; else "unknown version" |

**Removing outside the installer**, per plugin and for the whole suite:

- **Windows:** Settings → Apps lists *Myzel Audio* (everything) and one *Myzel Audio \<Name>*
  entry per installed plugin. A plugin entry runs the copy of the setup kept in
  `C:\Program Files\Myzel Audio\` with `/REMOVE=<plugin>`; the suite entry runs Inno's
  uninstaller. Removing the last plugin removes the suite entry too.
- **macOS:** `sudo "/Library/Application Support/Myzel Audio/uninstall.sh" [plugin…]` (all if
  none named): deletes the bundles and forgets the receipts (`pkgutil --forget`). Installed by
  the pkg, so it isn't quarantined. Running the pkg again and ticking a *Remove* row does the
  same.
- **Linux:** `./MyzelAudio-….run -- --remove <plugin…>` or `--remove-all`, or the copy of
  `setup.sh` the installer keeps beside its receipts (`~/.local/share/myzel-audio/setup.sh`).

**User data is never touched:** presets and settings live in
`<user app data>/Myzel Audio/<Plugin>/` (`PresetManager`, `ViewMode`), factory presets are
compiled in, and no installer or removal writes there.

**Non-interactive use** (for CI tests and power users), each OS's native form:

```
setup.exe /VERYSILENT /COMPONENTS="cerulean,umber" /TASKS="vst3,clap"
setup.exe /VERYSILENT /REMOVE="umber"
sudo installer -pkg MyzelAudio.pkg -target / -applyChoiceChangesXML choices.xml
./MyzelAudio.run -- --plugins cerulean,umber --formats vst3,clap [--system] --yes
./MyzelAudio.run -- --remove umber [--system]
./MyzelAudio.run -- --list        # status table for both scopes, changes nothing
```

Windows extras: `/REMOVE=<ids>` alone exits 0, 1 (unknown id) or 9 (a file in use); with
`/COMPONENTS` it ticks those *Remove* rows in a normal run. `/VERYSILENT /LIST=<file>` writes the
detection table as UTF-8 TSV (id, name, status `notinstalled|older|unknown|current|newer|notoffered`,
installed version, formats, offered version, text) and installs nothing. A Windows receipt whose
files are all gone counts as not installed and is dropped on the next install or removal. A silent
run downgrades without asking (the suppressed question answers *Yes*). Linux also takes
`--remove-all`, `--downgrade` (with `--yes`) and `MYZEL_UI=zenity|kdialog|whiptail|text`;
`MYZEL_SYSTEM_ROOT=<dir>` prefixes every system path and skips the password prompt (tests only).

## Install locations

| | Windows | macOS | Linux, per-user | Linux, system-wide |
|---|---|---|---|---|
| AU | — | `/Library/Audio/Plug-Ins/Components/<Name>.component` | — | — |
| VST3 | `C:\Program Files\Common Files\VST3\<Name>.vst3` | `/Library/Audio/Plug-Ins/VST3/<Name>.vst3` | `~/.vst3/<Name>.vst3` | `/usr/local/lib/vst3/<Name>.vst3` |
| CLAP | `C:\Program Files\Common Files\CLAP\<Name>.clap` | `/Library/Audio/Plug-Ins/CLAP/<Name>.clap` | `~/.clap/<Name>.clap` | `/usr/lib/clap/<Name>.clap` (the CLAP spec names no `/usr/local` path) |
| LV2 | — | — | `~/.lv2/<Name>.lv2` | `/usr/local/lib/lv2/<Name>.lv2` |
| Standalone | `C:\Program Files\Myzel Audio\<Name>.exe` + Start menu entry | `/Applications/Myzel Audio/<Name>.app` | `~/.local/bin/<Name>` + `~/.local/share/applications` entry and icon | `/usr/local/bin/<Name>` + `/usr/local/share/applications` entry and icon |
| `LICENSES.txt` | `C:\Program Files\Myzel Audio` | `/Library/Application Support/Myzel Audio` | `~/.local/share/myzel-audio` | `/usr/local/share/myzel-audio` |

`LICENSES.txt` (`myzel_write_licenses` in `installer/common.sh`) is `LICENSE`, `NOTICE` and the OFL of
every font the shipped plugins embed: the fonts' licence asks to go with each copy. It stays while
any plugin is installed and goes with the last one.

Linux system-wide runs the privileged part through `pkexec` in a desktop session, else `sudo`.

64-bit only: Windows `x64compatible` (an ARM64 Windows runs the installer, but only x64 hosts
load the plugins); macOS universal (arm64 + x86_64, as CI builds it); Linux x86_64.

## AU builds

AU is a build change outside the installers, and a prerequisite for the macOS installer:

- `myzel_add_plugin` adds `AU` to `FORMATS` on Apple. `PLUGIN_MANUFACTURER_CODE` (`Myzl`) and
  each plugin's `CODE` already give the AU its identity; effects register as `aufx`. The
  existing codes are frozen, so AU identity is fixed from the first build.
- `ci.yml` (macOS) runs `auval -v aufx <CODE> Myzl` on the built component and uploads it.
  `auval` finds components only in a scanned folder, so the CI step copies it to
  `~/Library/Audio/Plug-Ins/Components` first.
- Logic caches AU validation by component version: every release that changes a plugin bumps its
  `project()` version, or Logic keeps the old validation result.
- The macOS `postinstall` restarts the AU registry (`killall -9 AudioComponentRegistrar`) so a
  running Logic or GarageBand sees new and removed components after a rescan.
- `cmake/MyzelPlugin.cmake` and `ci.yml` have uncommitted edits from another session (LV2 class,
  Halite in the matrix); the AU change waits for those to land, then goes in as its own commit.

## Unsigned distribution

### macOS without a Developer ID

What can't be had without the paid Apple Developer Program: a Developer ID certificate and
notarisation. A free Apple ID's "Apple Development" certificate only runs on the owner's own
devices, so it doesn't help. What the plan uses instead:

1. **Ad-hoc signature on every binary.** Apple Silicon refuses to run arm64 code with no
   signature at all; an ad-hoc one (`codesign --force --sign - --timestamp=none`) is free and
   enough. The release workflow re-signs each component, bundle and app after the build (inner
   binaries first) and checks it with `codesign --verify --strict --deep`. Nothing touches the
   bundles after signing, because any change breaks the signature.
2. **The `.pkg` is the only thing Gatekeeper checks.** The browser quarantines the download;
   the user lets it through **once**:
   - macOS 15 and later: double-click → "not opened" dialog → *Done* → System Settings →
     Privacy & Security → *Open Anyway* → admin password. (The old right-click → *Open* bypass
     no longer works since macOS 15.)
   - Or in Terminal, before opening it: `xattr -d com.apple.quarantine ~/Downloads/MyzelAudio-*.pkg`
     (or `sudo installer -pkg … -target /`, which skips Gatekeeper).
3. **The installed files carry no quarantine flag**, because Installer writes them, not a
   browser. Hosts load ad-hoc-signed, unquarantined plugins without any prompt, and the
   Standalone apps open normally. The `postinstall` runs `xattr -dr com.apple.quarantine` over
   every installed bundle anyway. This makes item 3 hold even if Installer did propagate the
   flag, which is what lets the plan go ahead without a Mac.
4. **The download page and the pkg's welcome page** show the *Open Anyway* steps, so the first
   dialog is expected rather than alarming.

**Verifying without a Mac.** The GitHub macOS runner covers everything except the Gatekeeper
dialogs themselves:

- Mark the built pkg as downloaded (`xattr -w com.apple.quarantine …`), confirm
  `spctl --assess --type install` rejects it (the dialog the user will see), then install it
  with `sudo installer` and check that no installed file carries `com.apple.quarantine`.
- `codesign --verify` on every installed bundle, `auval` on every installed AU, pluginval on
  every installed VST3: the plugins load from their installed place with their ad-hoc signature.
- Removal through a *Remove* choice and through `uninstall.sh` leaves nothing behind.

What stays unseen is the *Open Anyway* click-path and the hosts' own GUI behaviour. The first
releases call the macOS installer **beta** in the release notes, and an issue template
(*macOS install report*: macOS version, chip, what dialogs appeared, which hosts loaded the
plugins) collects the first users' results. Screenshots for the welcome page come from those
reports. Optional: renting a cloud Mac for a day (a few euros) for one hands-on pass before the
first release.

### Windows without a code-signing certificate

1. **The installer `.exe` is unsigned.** The browser marks it (Mark of the Web) and SmartScreen
   shows "Windows protected your PC": *More info* → *Run anyway*. Then UAC asks for admin rights
   with "Publisher: Unknown". Both are documented on the download page with screenshots.
2. **The installed DLLs carry no Mark of the Web** (the installer writes them, not a browser),
   so hosts load them without any prompt. Nothing else on Windows checks plugin signatures.
3. **Defender false positives** on unsigned Inno installers happen now and then: the release
   checklist uploads each new installer to VirusTotal before publishing, and a flag is reported
   to Microsoft as a false positive.
4. **Free signing later, without changing the design:** the SignPath Foundation (*Open*) signs
   open-source projects' Windows releases for free from GitHub Actions. It slots in as one step
   in `release.yml` between building the setup and computing the checksums.

### Trust without a signature (all OSes)

- `SHA256SUMS` next to the installers in every release.
- GitHub build provenance (`actions/attest-build-provenance`, free for public repos): anyone can
  check that a file came from this repo's workflow at a given commit
  (`gh attestation verify <file> --repo mtunix/myzel-audio`).
- The workflow builds from the tag with no local inputs, so a release can be rebuilt from source.

## Extensibility

Adding a plugin to the installers is one line in `installer/plugins.tsv` once the plugin is
released:

```
# id        name      blurb
cerulean    Cerulean  A creative reverb: ten algorithms from rooms to clouds
mulberry    Mulberry  An echo: seven delay machines
umber       Umber     An analog-modelled saturator: five circuit models
```

Everything per-plugin is derived from that file and the plugin's own CMake:

- `release.yml` reads the list in a first job and passes it to the build matrix as JSON.
- `tools/installer.sh` generates the per-plugin parts: an Inno include
  (`plugins.generated.iss`: components, files, tasks, remove entries), the pkg's Distribution
  XML and component packages, and the plugin list embedded in the `.run`.
- Version: the plugin's `project()` line (as `package.sh` reads it). Formats: whatever the
  plugin's stage contains, so a plugin without a Standalone or AU needs no special case.
- Identifiers (pkg IDs, registry key, receipt path) follow from the plugin id.

The installer scripts themselves (`MyzelAudio.iss`, `distribution.xml.in`, `setup.sh`) never
name a plugin. A CI check fails the build if they do.

A later column (`preview`) can mark a plugin as offered but unticked by default, for in-development
plugins, without changing the scripts.

## Release pipeline

`.github/workflows/release.yml`, triggered by a `release-<suite version>` tag or
`workflow_dispatch` (a dry run that uploads the installers as workflow artefacts but makes no
release). The tag is pushed by the user; nothing here pushes.

```
plugins (reads plugins.tsv → matrix)
  └─ build (os × plugin)                    LTO on, tests, stage the formats, sign (mac ad-hoc),
                                            pluginval/auval on the staged bits, upload stage-<os>-<plugin>.tar
       └─ installer-linux / -windows / -macos   unpack that OS's stages (mac: codesign --verify),
                                                build the installer, install-test it
            └─ release                          SHA256SUMS, attestations, draft GitHub Release
```

- The build stage reuses `ci.yml`'s configure and build steps with `-DMYZEL_ENABLE_LTO=ON
  -DMYZEL_BUILD_TOOLS=OFF`; macOS pins `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` (JUCE 9 allows 10.11;
  raise it to 12.0 if the runner's Xcode reaches 27, which rejects lower targets). Linux builds on
  `ubuntu-24.04`.
- Each stage is `stage/<plugin>/<format>/…` plus a `version` file. It travels between jobs as a
  tarball, because `upload-artifact` drops exec bits and symlinks.
- `installer/macos/build.sh` signs its own copies of the bundles again (the stage stays as it is),
  so a local build from an unsigned stage works too.
- `tools/installer.sh <os> <stage-dir> <out-dir>` builds one installer; it calls `iscc`,
  `pkgbuild`/`productbuild` or `makeself`. It runs locally on the matching OS too, and on Linux
  it also builds the Windows setup under Wine.
- Pinned tools: makeself 2.7.1 (SHA-256 checked, xz, downloaded to `tools/.bin` when not on PATH);
  Inno Setup 6 from the runner image (6.7.1), else `choco install innosetup --version 6.7.1`; under
  Wine, Inno Setup 6.7.4 (SHA-256 checked) in a private prefix in `tools/.bin`; pluginval as in
  `tools/pluginval.sh` (v1.0.4).
- Windows jobs set `core.autocrlf false` before checkout, so Git Bash gets the scripts with LF.
- The release job's notes come from `plugins.tsv`; a re-run replaces its own draft and never
  touches a published release.
- Output names: `MyzelAudio-<suite>-windows-x64-setup.exe`, `MyzelAudio-<suite>-macos-universal.pkg`,
  `MyzelAudio-<suite>-linux-x86_64.run`.
- The release is a **draft**: the user reads it, then publishes.

**Install tests in CI** (on each OS's runner, after building its installer):

1. Fresh install of two plugins, two formats (Linux: once per scope); check the files, the
   receipt, and that nothing else was written.
2. Detection: `--list` / the Distribution and `[Code]` logic report them as up to date; a fake
   older receipt reports "update"; a hand-copied bundle with no receipt reports "unknown version".
3. Re-run with a third plugin ticked: the first two untouched, the third added.
4. pluginval (strictness 10) against the **installed** VST3; macOS also `auval` against the
   installed AU and `codesign --verify --strict`; no `com.apple.quarantine` on any installed file.
5. Remove one plugin through the installer, one through the OS path (Windows `/REMOVE=`, macOS
   `uninstall.sh`, Linux `--remove`): only that plugin's files and receipt are gone, the others
   still validate.
6. Full uninstall: everything the installer wrote is gone; the user data directory is untouched.

The tests need three plugins in `plugins.tsv` and a machine with no Myzel Audio install. The Linux
test refuses to run as root (its pkexec and sudo checks would write to the real `/usr`).
CI calls them as `xvfb-run -a bash installer/linux/test.sh <run> <stage>`,
`bash installer/macos/test.sh <pkg> <stage>` (passwordless sudo) and
`pwsh -NoProfile -File installer/windows/test.ps1 -Installer <exe> -Stage <stage>` (elevated), with
absolute paths and `PLUGINVAL`, `PLUGINVAL_STRICTNESS=10` set.

## Build contract

Every installer is built the same way, so `release.yml` and local builds call the same scripts:

```
tools/stage.sh <plugin> <build-dir> <stage-dir>        copy one plugin's Release formats into the stage
tools/installer.sh <linux|windows|macos> <stage-dir> [out-dir]   → installer/<os>/build.sh <stage-dir> <out-dir>
installer/<os>/test.sh <installer-file> <stage-dir>    install tests 1–6 (Windows: test.ps1 -Installer -Stage)
```

The stage, one directory per plugin in `plugins.tsv`, folder names as JUCE's artefact folders:

```
<stage>/<plugin>/version                    the plugin's project() version, e.g. 0.1.0
<stage>/<plugin>/VST3/<Name>.vst3/          all OSes
<stage>/<plugin>/CLAP/<Name>.clap           file on Linux and Windows, bundle on macOS
<stage>/<plugin>/LV2/<Name>.lv2/            Linux
<stage>/<plugin>/AU/<Name>.component/       macOS
<stage>/<plugin>/Standalone/<Name>          Linux; <Name>.exe on Windows, <Name>.app on macOS
```

An optional `<stage>/<plugin>/icon.svg` becomes the Linux menu icon; without it `setup.sh`
draws the initial. The `.run` payload is `setup.sh`, `plugins.list` (id, name, version, formats,
blurb), `suite`, `glibc` (the highest `GLIBC_` symbol any staged binary needs; `setup.sh` refuses an
older system) and `stage/<plugin>/<format>/`. Menu entries are `myzel-audio-<plugin>.desktop` with
icon `myzel-audio-<plugin>.svg`.

`installer/common.sh` (bash 3.2, sourced) reads `plugins.tsv`, `VERSION` and the stage: plugin
ids, names, blurbs, versions, formats. Tests run headless and need no GUI; when `PLUGINVAL`
names a pluginval binary, they also validate the installed VST3.

## Repository layout

```
installer/
  PLAN.md
  VERSION                 suite version, e.g. 2026.11.0
  plugins.tsv             id, display name, blurb: the plugins that ship
  common.sh               shared helpers for the build scripts
  <os>/build.sh           builds that OS's installer (the build contract); <os>/test.sh (Windows: test.ps1) runs tests 1–6
  windows/MyzelAudio.iss  Inno script; [Code] does detection, status column, /REMOVE, /LIST
  macos/distribution.xml.in, scripts/{preinstall,postinstall,remove}, uninstall.sh, resources/ (welcome, licence)
  macos/formats.sh        format → folder table for build.sh and test.sh (uninstall.sh keeps its own copy: it ships alone)
  linux/setup.sh          checklist, detection, scopes, install, --list, --remove
  docs/                   Gatekeeper and SmartScreen screenshots for the README and welcome pages
tools/stage.sh            stages one plugin's built formats
tools/installer.sh        builds one OS's installer (installer/<os>/build.sh generates the per-plugin parts)
.github/workflows/release.yml
.github/ISSUE_TEMPLATE/macos-install-report.yml   (an issue form)
```

Note: `.gitignore` ignores `/plugins/*/PLAN.md` only, so this `installer/PLAN.md` is tracked
unless the pattern is extended.

## Milestones

| | Milestone | Done when |
|---|---|---|
| M0 | **AU and spikes** | AU builds for every plugin and passes `auval` in CI. On the macOS runner, a hand-built ad-hoc-signed pkg with one plugin installs from a quarantined file with no quarantine on the payload, and its AU and VST3 validate from the installed place. An unsigned Inno setup installs on the Windows runner and its VST3 validates from `Common Files`. A `.run` installs on Ubuntu 24.04 in both scopes |
| M1 | **Pipeline** | `release.yml` builds and stages every plugin in `plugins.tsv` on three OSes, signs macOS ad-hoc, writes `SHA256SUMS` and attestations, makes a draft release (with the existing tarballs as placeholders) |
| M2 | **Linux** | `.run` with both scopes, checklist on all four dialog back-ends, detection, receipts, `--list`, per-plugin `--remove`, `.desktop` entries; install tests green |
| M3 | **Windows** | Inno setup with components, format tasks, status column, *Remove* group, per-plugin Settings → Apps entries (`/REMOVE=`), Restart Manager, uninstaller; install tests green |
| M4 | **macOS** | pkg with the Customize pane shown by default (`customize="always"`), install choices pre-selected from detection, *Remove* choices visible only for installed plugins, `uninstall.sh`, quarantine strip, AU registry restart; install tests green; issue template in place |
| M5 | **Docs and first release** | Root `README.md` gets an *Install* section per OS (with the *Open Anyway* and *Run anyway* steps, and removal), each plugin's `MANUAL.md` points to it, `package.sh`/README text updated; first `release-*` tag pushed and the draft published by the user, macOS marked beta |

## Measured status (2026-11-14)

All four milestones' scripts are written (M1–M4); none has run on GitHub yet.

**Verified locally (Linux):**

- Linux: `tools/installer.sh linux` builds the `.run` from a build-dev stage of the three plugins
  (16 MB with xz, about 80 s). `xvfb-run -a installer/linux/test.sh` passes tests 1–6 in both
  scopes, with pluginval at strictness 10 on the installed VST3s, plus detection, glibc refusal,
  dialog failures, a dismissed password prompt, and the zenity, kdialog and whiptail back-ends
  through fakes. Real zenity 4 and whiptail were driven by hand; `kdialog` was faked only.
- Windows: `tools/installer.sh windows` builds the setup under Wine with Inno Setup 6.7.4 from a
  fake stage. Tests 1, 2, 3, 5 and 6 and the last-plugin case pass under Wine (by hand, not through
  `test.ps1`); the wizard pages were checked in Xvfb screenshots.
- macOS: `MYZEL_PKG_GENERATE_ONLY=1` generates the Distribution, resources and `uninstall.sh` on
  Linux. The Distribution passes a structural check, its JavaScript passes a node simulation of the
  default ticks and the *Remove* rule, and `uninstall.sh` and the pkg scripts pass against a
  temporary root with mocked `pkgutil`.
- `release.yml`: YAML parses, every bash step passes `bash -n`, and the plugin-name check passes on
  the tree and fails on planted names. A configure-only run of the AU change on Linux passes.

**Only CI can verify:** every step on a real runner; `test.ps1` (no pwsh here) and real UAC,
Settings → Apps and Restart Manager on Windows; all of macOS (`pkgbuild`, `productbuild`,
`codesign`, `installer`, `spctl`, `auval`, the Distribution JavaScript in Installer itself, the
AU build); real pkexec and sudo on Linux.
`actionlint` 1.7.12 passes on `release.yml` and `ci.yml`.

## Settled (2026-11-14)

- First installers ship Cerulean, Mulberry, Umber and Halite (Halite added 2026-11-20); the installers stay generic (*Extensibility*).
- AU builds on macOS (*AU builds*).
- Linux builds on Ubuntu 24.04 for now.
- Linux offers per-user and system-wide installs.
- Per-plugin removal on every OS.
- No Mac available: macOS is verified on the CI runner and by first users (*Verifying without a Mac*).

## Open

1. **SignPath Foundation for Windows.** Free code signing for open-source projects: the release
   workflow sends the built setup to SignPath, which signs it with a certificate issued to the
   SignPath Foundation (the publisher shown is "SignPath Foundation", not "Myzel Audio"). It
   checks that the file was built by this repo's CI from public source. It needs an application,
   and their terms (OSI licence, active project, a code-signing policy page) should be checked
   at the time. It removes "Publisher: Unknown"; SmartScreen still builds reputation over time,
   but across releases rather than per file. Proposal: apply after the first release.
2. **Older Linux distros.** Ubuntu 24.04's glibc 2.39 excludes Debian 12 and Ubuntu 22.04. If
   users ask, build in an older container with a newer compiler.
3. **Linux on ARM64** (`ubuntu-24.04-arm` runner, free for public repos): later, if asked.
4. **Updating with fewer formats.** On every OS, formats not ticked in an update stay at the
   older version while the receipt names the new one, so detection says "up to date" over a stale
   CLAP, say. Options: refresh every format already installed, or keep a version per format.
5. **libstdc++ on Linux.** Only glibc is checked; the binaries also need `GLIBCXX_3.4.32`
   (GCC 13.2's libstdc++). A distro with glibc 2.39 and an older libstdc++ fails at load time.
