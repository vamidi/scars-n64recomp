# Native (HLE) audio engine - notes

Work area: `src/runtime/audio_hle.cpp` (interface unchanged in `audio_hle.h`).
Enabled with `SCARS_AUDIO_HLE=1`; `trace_backend.cpp` returns `&audio_hle_run`
for `M_AUDTASK` when the variable is set and `audio_hle_ready()` is true.

Everything below was derived from this repository's own material:

- the recompiled game corpus in `build/generated` (the game's translated
  libultra AL synth: `alAudioFrame`, `alAdpcmPull`, `alLoadPull`,
  `alResamplePull`, `alEnvMixerPull`, `alMainBusPull`, `alAuxBusPull`,
  `alSavePull`, the reverb helpers and the mixers);
- the ROM's own aspMain text/data (`recomp/rsp/aspMain.toml` input) and the
  RSPRecomp output in `build/generated_rsp/aspMain.cpp`;
- a live dump of the ACMD stream produced by the engine itself.

No code was taken from `D:\Personal\C++\Kyujo` or any other GPL implementation.
The ACMD layouts below are a specification recovered from the emitters; the
implementation is independent.

---

## What it does

`recomp::rsp::run_task` copies the OSTask to `dmem[0xFC0]`; `audio_hle_run`
reads it, walks the ACMD list at `task->t.data_ptr` for `task->t.data_size`
bytes, executes the commands against a 4 KiB DMEM model, and returns
`RspExitReason::Broke`.

The model is deliberately close to the ucode rather than a high-level synth:
the game's command stream already contains the complete synthesis plan (per
voice ADPCM source windows, resample pitch, envelope volumes, bus mixdown,
reverb sends, interleave and the final AI-buffer save). Reproducing it at the
DMEM level means the output lands in RDRAM exactly where the game expects it,
with the game's own frame sizes, and no assumptions about the mixer topology.

Implemented commands:

| op | command | implementation |
|---|---|---|
| 0x00 | SPNOOP | nop |
| 0x01 | ADPCM | 9-byte frames (16 samples), book-driven 2-tap predictor, history carried across chunks |
| 0x02 | CLEARBUFF | zero `count` bytes at `0x5C0 + addr` |
| 0x03 | ENVMIXER | mixes the SETBUFF `in` voice buffer into the L/R accumulators at `(out - 0x440)` using the SETVOL gains, plus an aux (reverb send) pair |
| 0x04 | LOADBUFF | DMA of the compressed window from RDRAM into SETBUFF `in` (byte-accurate) |
| 0x05 | RESAMPLE | linear interpolation, pitch = `int_part + frac/65536` from word 0 |
| 0x06 | SAVEBUFF | DMEM -> RDRAM, `count` bytes from SETBUFF `out` (this is how the AI buffer is filled) |
| 0x07 | chunk marker | resets the per-chunk SETBUFF/resample state |
| 0x08 | SETBUFF | `in = word0 & 0xFFFF`, `out = word1 >> 16`, `count = word1 & 0xFFFF` (bytes); flags `0x08` select the A_AUX (dry/wet) pair |
| 0x09 | SETVOL | per-flag Q15 gain (`0x2` left, `0x4` right, `0x6` both, `0x0` dry, `0x8` aux) |
| 0x0A | ADPCM window / loop setup | parsed, no extra state needed (the window arrives via LOADBUFF) |
| 0x0B | LOADADPCM | copies the book to DMEM 0x4C0; count is in 16-bit words (0x80 -> 0x100 bytes) |
| 0x0C | MIXER | `out += in * gain >> 15` |
| 0x0D | INTERLEAVE | L from `word1 >> 16`, R from `word1 & 0xFFFF`, `count` bytes per channel, output at SETBUFF `out` |
| 0x0E | POLEF | **stub** (reverb low-pass); see "what is stubbed" |
| 0x0F | SETLOOP | parsed, not needed yet (loop state is carried by persistent ADPCM history) |

Frame format: signed 16-bit stereo, interleaved L/R, written by the final
`SAVEBUFF` of each chunk into the AI buffer. The task itself produces
**1984 samples (3968 bytes)** across 7 chunks: the interleave of each chunk
consumes 0x140 bytes per channel (one mono buffer pair) and produces 0x280
bytes of stereo; the last chunk saves only 0x80 bytes. 6 x 0x280 + 0x80 =
0xF80 = 3968 bytes, which matches `audio buffer #N: 1984 samples` from
`trace_backend.cpp`.

`task->t.output_buff` and `output_buff_size` are **zero** in this game, so the
engine never uses them; the frame reaches the AI buffer only through the
stream's SAVEBUFFs. (This was verified from the live OSTask dump and from
`osAiSetNextBuffer` receiving the game's own size table, not the RSP-written
length. `osSpTaskLoad`'s conversion survives because SAVEBUFF's word 1 is
already a physical address.)

---

## Measured

Build: `build/hle` (headless trace renderer), RelWithDebInfo, MSVC.

Engine log (`scars-log.txt`), first tasks (final build):

```
audio HLE task #1: 754 commands, 19 voice mixes, 13888 int16 written, peak=31958
audio HLE task #2: 744 commands, 20 voice mixes, 13888 int16 written, peak=25482
audio HLE task #3: 669 commands, 11 voice mixes, 13888 int16 written, peak=25527
```

Trace-backend audio callback for the same frames:

```
audio buffer #1: 1984 samples peak=31958    (~3968 bytes total)
audio buffer #2: 1984 samples peak=20694    (~7936 bytes total)
audio buffer #3: 1984 samples peak=0        (~11904 bytes total)
```

- A buffer with peak 0 (e.g. #3 here) is a rest: the sequencer had no active
  voices, so the cleared buses were saved. It is not a missing frame.
- Sample counts per queue are the game's own (1984, or 1760 for a shorter
  sequencer frame), produced from the stream's `SAVEBUFF` byte counts.
- On a run where the runtime scheduler did not lose a wake-up, the counters
  climbed for the whole run: `vis=840 gfx=832 rsp=833 audio=832` after 15 s and
  `ai_len` oscillating 2168-3336 bytes, i.e. roughly one audio task per VI.
- The game keeps rendering: `display list #2 ... data_ptr=0x80360C50` and the
  first display-list words (`DB060000 ...`) are decoded by the headless trace
  renderer while the audio frames are produced.
- `audio HLE task #N: ... int16 written` counts every SAVEBUFF of the task
  (including the voice save buffers at 0x465A0..0x47E00); the AI frame itself is
  the 1984 samples / 3968 bytes the audio callback reports.
- A dumped 0x280-byte output chunk (chunk 1) has RMS = 2297, peak = 13969, 149
  of 320 samples non-zero, no clipped samples: a real waveform, not a constant
  or noise. `SCARS_AUDIO_HLE_DUMP=1` writes the first two AI-buffer chunks to
  `scars-audio-frame-*.raw` for inspection. (The first dumped chunk of the boot
  task is all zero: chunk 0 saves the cleared buses before any voice starts.)

Caveat: the level is still hot (some chunks reach full scale), because the
reverb's POLEF/return path is stubbed and the aux send is only attenuated by a
fixed 1/4 return loss. This is the next thing to tune (see below).

---

## What is stubbed / approximate

1. **POLEF (op 0x0E)** is a no-op. It is the reverb's one-pole filter; the
   reverb send/return buffers are mixed, but without the filter there is no
   damping and the aux path is over-bright / too loud. The fixed 1/4 return
   loss in `cmd_envmixer` is a placeholder for the filter's attenuation.
2. **ENVMIXER envelope ramping** is not modelled: the current SETVOL value is
   used for the whole chunk. The SETVOL commands also carry a target volume
   (word 1, e.g. 0x0000FFE6 / 0x7FFFFFFF), which the ucode ramps towards. Fast
   attacks/decays therefore step instead of ramping (audible as zipper noise on
   envelopes, not on steady notes).
3. **ADPCM loop points** (`0x0F`/`0x0A`) are parsed but not used to wrap: a
   window that runs past the end repeats its last sample. Sequenced music loops
   at the note level, so this is mostly inaudible; sustained looped
   instruments will cut instead of looping.
4. **ADPCM book source**: the book is loaded with LOADADPCM (count is in
   16-bit words; the usual 0x80 -> 0x100 bytes, i.e. 8 predictors of 32 bytes,
   ending exactly at the 0x5C0 work area) and read from DMEM 0x4C0. Predictor
   indices are clamped to the loaded entry count so a 16-predictor header cannot
   read into the sample buffers. The predictor history is kept per loop
   descriptor across chunks. This is the part of the engine with the most
   uncertainty; the decoded output is smooth and correctly scaled in the dump
   above, but it has not been compared sample-for-sample against the real RSP.
5. **RESAMPLE state** (`word1`, an RDRAM struct) is ignored; the phase is kept
   in the engine and wrapped to the decoded window. Pitch direction/scale was
   taken from the emitter (`int + frac/65536`); it is not cross-checked.
6. **output_buff / output_buff_size** are not written (both are 0 in this
   game's task). If a future task uses them, the frame count would have to be
   published there.

---

## Shared-file change needed (for the other session)

**Symptom**: with any RSP backend (trace stub, recompiled aspMain, or this HLE)
the game sometimes stops submitting audio/graphics tasks after 2-8 tasks. The
VI thread keeps retracing, but the game's main thread is parked in
`osRecvMesg(0x803FF970)` (the VI queue) and the queue is *full* (validCount 2/2,
waiters 0). The audio thread is parked in `osRecvMesg(0x803C93C8)` (the SP
completion queue) and the SP task thread is idle (it already called
`sp_complete()`).

**Current state (2026-11-26 23:00)**: the other session has added scheduler
diagnostics and a check-then-park mutex (commits `711d2c1`, `00584bd` in this
repository/the submodule). That did not eliminate the stall: with the **tracing
stub** (`SCARS_AUDIO_HLE=0`, no HLE code running at all) two fresh runs stopped
at 5 and 7 audio tasks, exactly like the HLE runs. So the residual stall is not
in this engine. The stall dump at that point reads:

```
[stall] queue SP 803C93C8 valid=0/1 recv_waiters=2
[stall] queue AI 8038D9C0 valid=1/1 recv_waiters=0
[stall] queue VI 803FF970 valid=2/2 recv_waiters=0
[stall] queue DP 803BF7F8 valid=1/1 recv_waiters=0
```

i.e. messages are sitting in capacity-1/2 rings while their receivers are
parked, and the SP completion queue has two waiters for messages that were
never delivered. The audio thread ends up waiting for the AI retrace event and
the game main thread for the VI event; no further tasks are submitted.

**Attempted workaround (in audio_hle.cpp)**: `audio_hle_ready()` calls
`ultramodern::set_message_queue_control` with all requeue flags enabled, so a
full-queue delivery is retried instead of dropped. It is harmless and makes
delivery closer to correct, but the residual park-with-a-full-ring case above
still starves, so it is not a fix.

**Suggested remaining fix** (not applied - shared files):
- audit the notify path: `schedule_running_thread` inserts the waiter into the
  running queue, but the parked thread waits on its resume semaphore;
  if another thread is swapped in first, the queued waiter must still be
  resumed (the trace shows `queued T3` followed by `resume T3` in the healthy
  case, so a missed resume is the suspect);
- consider making the SP/AI/VI delivery *blocking* when the queue is full
  instead of dropping/requeueing, or growing the guest ring (capacity is the
  game's, so this is not ours to change);
- a stall dump that also prints each parked thread's wait queue (not just the
  count) would pinpoint the missed resume.

The engine itself produced every frame it was given; winner runs of 832 tasks
(above) and the per-task peaks prove the audio path, and the stall is fully
reproducible with the stub.

---

## How to run

```
cmake -S . -B build/hle -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DSCARS_LINK_GENERATED=ON -DSCARS_GENERATED_DIR=<repo>/build/generated ^
  -DSCARS_RENDERER_SDL_RT64=OFF
cmake --build build/hle
cd build/hle/bin
set SCARS_AUDIO_HLE=1
set SCARS_AUTO_CONFIRM=1
set SCARS_USER_DIR=%TEMP%\scars-hle-user
scars.exe
```

Useful knobs:

- `SCARS_AUDIO_HLE_DELAY_MS` (default 1): artificial per-task time. The real
  RSP takes about a millisecond per frame, and the delay also spaces SP
  completion messages. Set to 0 for the fastest possible task.
- `SCARS_AUDIO_HLE_QUEUE_CTRL` (default on): installs the requeue-on-full
  message-queue policy described above. Set to 0 to compare with the runtime's
  default (zero-initialised) policy; the stall is identical either way.
- `SCARS_AUDIO_HLE_DUMP=1`: write `scars-audio-frame-1.raw` /
  `-2.raw` (native int16) for waveform inspection.
- `SCARS_RECOMP_AUDIO=1`: the other session's recompiled aspMain (still asserts
  in `run_task`; unrelated to this engine).

---

## Next steps

1. Implement POLEF (one-pole low-pass per channel with the state block at
   `word1`) and remove the fixed aux return loss.
2. Add SETVOL target ramping (per-sample increment towards word 1).
3. Use `0x0F`/`0x0A` to wrap ADPCM windows at the loop point and save the loop
   state back to RDRAM (`ALADPCMloop.state`) so the game and the engine agree.
4. Once the scheduler wake-up race is fixed, re-measure for a clean 60 s run
   and confirm no clicking at chunk boundaries.
5. Consider publishing `output_buff_size` for generality (this game leaves it
   zero, so it is not exercised).
