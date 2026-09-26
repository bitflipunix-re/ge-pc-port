# ARM-GE Discord server kit

This document defines the public Discord layout for the ARM-GE / GoldenEye 007 native ARM64 port.

The server should be useful to three groups without mixing their traffic:

1. people discovering the project;
2. testers reporting real-device behavior;
3. developers following the native ARM64/GLES port.

The public server should not distribute ROMs or generated ROM-derived sidecars.

## Recommended server structure

### START HERE

#### #welcome

Read-only landing channel.

**Pinned message**

> # ARM-GE — GoldenEye 007 native ARM64 port
>
> ARM-GE is an open-source engineering effort to run the reconstructed GoldenEye 007 codebase natively on ARM64 Linux handhelds.
>
> **Reference target:** R36S / dArkOSRE / PortMaster  
> **Runtime:** AArch64 + SDL2 + OpenGL ES  
> **Status:** experimental alpha
>
> This is not N64 emulation. The project is porting the reconstructed game code to a modern 64-bit ARM Linux host and working through LP64, graphics, runtime and gameplay correctness issues.
>
> Start here:
>
> • #project-status — current verified state  
> • #main-builds — automatic CI results from the GitHub `main` branch only  
> • #alpha-testing — real-device testing and results  
> • #support — installation and usage questions  
> • #bug-reports — reproducible defects and logs  
> • #development — technical port discussion
>
> **ROM policy:** no ROMs are distributed here. Testers must provide their own legally obtained GoldenEye 007 NTSC-U big-endian ROM.
>
> Expected ROM SHA-1: `abe01e4aeb033b6c0836819f549c791b26cfde83`
>
> Maintainers: bitflipunix and Tomobobo710.

#### #project-status

Read-only concise project state. Update manually only when a real milestone changes.

Suggested pinned format:

> **Current target:** R36S / ARM64 Linux / PortMaster  
> **Graphics:** SDL2 + GLES  
> **Current state:** boot, menus, intro and in-mission gameplay demonstrated on real hardware.  
> **Active work:** stage correctness, spawn/state issues, AI/objectives/props, collision/navigation edge cases, GLES rendering defects, audio/runtime stability and broader handheld compatibility.
>
> Latest authoritative source: GitHub `main`.

#### #rules-and-rom-policy

Read-only.

Keep this short:

> • No ROM uploads, ROM links, ROM requests or generated ROM-derived sidecars.  
> • Logs, screenshots, source-level debugging and original patches are welcome.  
> • Use #bug-reports for reproducible bugs and attach `log.txt` where possible.  
> • Keep #main-builds discussion-free; use the linked discussion channels instead.  
> • Do not present unverified test results as confirmed fixes.

### BUILDS & RELEASES

#### #main-builds

Read-only for members. Webhook/bot write only.

Purpose: automatic build status from `bitflipunix-re/ge-pc-port` **main branch only**.

The repository workflow `.github/workflows/discord-main-builds.yml` posts completion status for push-triggered runs of the existing `GoldenEye AArch64 R36S GLES Build` workflow.

Repository secret required:

`DISCORD_WEBHOOK_MAIN_BUILDS`

Create a Discord webhook in this channel, then save its URL as that GitHub Actions repository secret.

Expected post format:

> ✅ **BUILD PASSED — main**  
> `26ec893` — assets: fix D303 muzzle-flash opcode-22 vertex truncation  
> [GitHub Actions run]

Do not mirror PR builds, feature branches or experimental branches into this channel.

#### #releases

Read-only announcements for deliberately published alpha packages and release notes. Do not treat every successful CI run as a release.

### TESTING & SUPPORT

#### #alpha-testing

General real-device testing discussion.

Pinned request:

> When testing, include:
> • device / CFW  
> • exact build or commit  
> • level / mission / situation  
> • what you expected  
> • what actually happened  
> • whether it reproduces  
> • `log.txt` if available

#### #bug-reports

Use a Discord Forum channel if possible.

Recommended forum tags:

`crash` `rendering` `gameplay` `audio` `input` `installer` `performance` `needs-log` `confirmed` `fixed-main`

Forum post template:

> **Build / commit:**  
> **Device / CFW:**  
> **Mission / location:**  
> **Reproduction steps:**  
> **Expected:**  
> **Actual:**  
> **Frequency:** always / intermittent / once  
> **Log attached:** yes / no  
> **Screenshot/video:** optional

#### #logs

Raw tester logs and larger diagnostic dumps.

Use threads per report rather than allowing unrelated logs to interleave.

Pinned guidance:

> Upload the complete log where possible. Include the exact build/commit and what happened immediately before the failure. Do not trim the first-launch, stage-load or fatal/crash section unless Discord upload limits require it.

#### #support

Install, ROM verification, controls, PortMaster launch and ordinary user questions.

Use this instead of mixing support into #development.

#### #questions

For questions already appearing in general chat that need an authoritative answer or should become documentation.

When a question repeats, move the resolved answer into #faq.

#### #faq

Read-only distilled answers from recurring #support and #questions traffic.

Initial topics:

• Which ROM version is required?  
• Where does the ROM go?  
• Is this emulation?  
• Which devices are currently verified?  
• Where do I find my log?  
• How do I identify my build/commit?  
• What should I include in a bug report?  
• What does a successful CI build mean versus a verified real-device fix?

### DEVELOPMENT

#### #development

General technical discussion for the ARM64/GLES port.

#### #arm64-lp64

Pointer width, signedness, ABI, binary-layout, segmented-address and host-vs-N64 address semantics.

#### #gles-rendering

Fast3D/GLES renderer work, graphics defects and shader/state debugging.

#### #gameplay-correctness

Stage/setup/model semantics, AI, objectives, props, collision, navigation, spawn/state and mission transitions.

#### #portmaster-r36s

Packaging, launcher, controls, device compatibility and R36S-specific runtime behavior.

#### #dev-logs

Curated engineering logs or traces that are useful for active debugging. Keep raw public tester dumps in #logs.

### COMMUNITY

#### #general

General project discussion.

#### #showcase

Screenshots, videos and progress clips.

#### #contributors

Coordination for people actively submitting patches, tests, documentation or hardware results.

## Roles

Keep roles simple:

- Maintainer — server/repo maintainers
- Contributor — code/docs contributors
- Alpha Tester — regular real-device testers
- Developer — technical discussion access/identity
- Community — default verified member
- Build Bot — webhook/bot identity

Do not gate ordinary technical channels behind unnecessary roles while the project is trying to attract testers and contributors.

## Channel permissions

Read-only to normal members:

- #welcome
- #project-status
- #rules-and-rom-policy
- #main-builds
- #releases
- #faq

Writable:

- testing/support/development/community channels

For #main-builds, allow only the webhook/bot and maintainers to post. Members should create discussion in #development or #alpha-testing rather than replying there.

## Intake workflow

When somebody posts a crash or malformed question in #general:

1. move or redirect it to #bug-reports or #support;
2. get the exact build/commit;
3. get device/CFW;
4. request the full log;
5. determine whether it reproduces on current `main`;
6. tag it confirmed only after reproduction or strong matching evidence;
7. when fixed in `main`, tag fixed-main and link the commit/build.

This preserves a clean distinction between:
- reported;
- reproduced;
- source-fixed;
- CI-passed;
- real-device verified.

## Main-branch build automation

The Discord notifier intentionally listens to the existing CI workflow rather than rebuilding anything.

Trigger chain:

`push to main -> build-r36s.yml -> workflow_run completed -> discord-main-builds.yml -> #main-builds`

It ignores:
- pull-request runs;
- feature branches;
- cancelled experimental work not originating from a main push.

This keeps #main-builds as the authoritative chronological record of the default branch rather than a firehose.
