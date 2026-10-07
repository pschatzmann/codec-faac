# FAAC - ISO/MPEG 2/4 AAC Encoder Library

## Contents

- [Scope](#scope)
- [Interface description](#interface-description)
- [Building and linking](#building-and-linking)
- [Supported streams](#supported-streams)
- [API (faac.h)](#api-faach)

  - [Calling sequence](#calling-sequence)
  - [Error handling](#error-handling)
  - [Function reference](#function-reference)
  - [Complete ADTS encode example](#complete-adts-encode-example)
  - [Defaults and PCM input](#defaults-and-pcm-input)
  - [Channel ordering and ownership](#channel-ordering-and-ownership)
  - [Custom allocators](#custom-allocators)
  - [Memory budget and embedded targets](#memory-budget-and-embedded-targets)
  - [Encoder delay and gapless output](#encoder-delay-and-gapless-output)
  - [Rate control modes](#rate-control-modes)
  - [Capping the peak frame size](#capping-the-peak-frame-size)

- [Porting from the legacy faacEnc* API](#porting-from-the-legacy-faacenc-api)
- [Porting from libfaac ABI 1](#porting-from-libfaac-abi-1)
- [ABI compatibility](#abi-compatibility)

## Scope

This document describes the interface and usage of the
FAAC - ISO/MPEG 2/4 AAC Encoder Library
Developed for the Freeware Advanced Audio Coding project.

## Interface description

The ISO/MPEG 2/4 AAC Encoder Library provides a high-level
interface for encoding PCM into MPEG-2 or MPEG-4 AAC access units.
The application handles file/container I/O, resampling, timestamps and transport.
The library emits RAW AAC or ADTS; it does not write MP4/M4A containers. A single header file is
provided for usage in C/C++ programs:

`faac.h`: the `faac_*` API — function prototypes and types.

The encoder is the shared library `libfaac` (`libfaac.so.2`,
`libfaac.2.dylib` or `libfaac.dll`; the build also produces the static archive). The
`faac` command-line encoder in `frontend/` is the reference
user of the API.

## Building and linking

Include `<faac.h>`. Build and install the encoder with Meson:

```sh
meson setup build-faac -Dfrontend=false
meson compile -C build-faac
meson install -C build-faac
```

The default builds shared and static libraries. Select one with
`-Ddefault_library=shared` or `-Ddefault_library=static`. Omit
`-Dfrontend=false` to also build the command-line encoder.

Compile the example below against an installed library:

```sh
cc -std=c11 -Wall -Wextra adts_encode.c -o adts_encode $(pkg-config --cflags --libs faac)
```

For static linking, use `pkg-config --cflags --libs --static faac` and select
an installation containing only the static library, or use your toolchain's
archive-selection options. `--static` adds private dependencies but does not
force archive selection. For a custom prefix, set `PKG_CONFIG_PATH` and the
platform's shared-library runtime search path. Remove old FAAC include and
library paths when migrating.

## Supported streams

| Feature | Support and requirements |
|---|---|
| AAC-LC | Supported by every encoder build; 1024 samples/channel per frame |
| HE-AAC v1 | Dual-rate SBR; input rate at least 32000 Hz; 2048 samples/channel per frame |
| HE-AAC v2, MAIN, SSR, LTP, LD, ELD, USAC | Unsupported |
| ADTS | Library supplies each frame's header |
| RAW AAC | One access unit per emitted frame; MPEG-4 ASC available separately |
| MP4/M4A | Mux outside the library using RAW access units and ASC |
| MPEG-2 | AAC-LC; no ASC; explicit HE-AAC resolves to MPEG-4 |
| Channels | 1–6 or 8, within the compiled `max-channels` ceiling; 7 is unsupported |
| PCM | Interleaved native-endian signed integer or float; see below |

Query `faac_get_library_info()` for `max_channels`, version/copyright and
`sbr_decimation` (build-time SBR analysis density; 1 is full density).
`max-channels` defaults to 8; `sbr-decimation` defaults to 1.
Use standard AAC sample rates (8000, 11025, 12000, 16000, 22050, 24000,
32000, 44100, 48000, 64000, 88200 or 96000 Hz). Validation accepts
7350–96000 Hz, but signaling uses a table index rather than an arbitrary-rate
ASC escape value. The library does not resample arbitrary input to that table.
For HE-AAC, query the resolved full rate after open.

## API (faac.h)

The API supplies all encoder parameters once, up front, to
`faac_encoder_open()`. The encoder therefore never exists in a
half-configured state, and every derived quantity (frame size, output-buffer
bound, effective sample rate, resolved object type) is known and queryable the
instant open returns. Every fallible call returns a `faac_status` code;
fixed-width integer types and width-pinned enums keep the ABI identical across
64-bit platforms (LP64 and LLP64), regardless of -fshort-enums. 32-bit targets
have their own layout because of pointer fields.
See [ABI compatibility](#abi-compatibility).

### Calling sequence

- Initialize a `faac_params` with
  `faac_params_init(&p, sizeof p)`. This is mandatory — it
  supplies defaults, stamps `struct_size` and clears padding, which is how the struct stays
  compatible as it grows in future releases; the size argument keeps a newer
  library from writing past an older caller's struct.
- Set the fields you care about (at minimum `sample_rate` and
  `num_channels`), then call `faac_encoder_open()`.
- Fill a `faac_encoder_info` with `faac_encoder_get_info()` (set its
  `struct_size` first) and size your buffers from it: input PCM holds
  `info.frame_samples` × `num_channels` samples; the output buffer
  must be at least `info.max_output_bytes`. The same struct also reports the
  resolved sample rate, object type, and rate-control settings. If you write a
  raw stream, fetch the AudioSpecificConfig with `faac_encoder_asc()`
  (library-owned — do **not** free it).
- Call `faac_encoder_encode()` for each block of input; it emits at most
  one frame per call and reports the byte count via an out-parameter.
  `in_samples` counts samples across all channels, not bytes or samples/channel.
  Supply at most `info.frame_samples * num_channels`, in whole channel groups.
  Smaller blocks accumulate internally; zero output during normal input means
  accumulation or priming, not end of stream.
- Pass `in_samples` == 0 to flush; keep calling until
  `bytes_written` is 0.
- Call `faac_encoder_close()`, passing the address of your handle; it is
  set to NULL on success.

### Error handling

Every fallible entry point returns a `faac_status`. `FAAC_OK` is 0 and
all error codes are negative, so `status < 0` tests for failure.
`faac_strerror()` maps any status to a static human-readable string (never
NULL). Notable codes: `FAAC_ERR_INVALID_ARGUMENT` (NULL, bad
`struct_size`, or an out-of-range field), `FAAC_ERR_UNSUPPORTED` (an
object type this build does not implement, or a request with no ASC),
`FAAC_ERR_OUTPUT_TOO_SMALL`, and `FAAC_ERR_INPUT_OVERFLOW`.

Always provide a non-NULL output buffer with at least `info.max_output_bytes`,
including while flushing. `bytes_written` must also be non-NULL; input may be
NULL only when `in_samples` is zero. Argument, capacity and per-call sample-limit
checks happen before consuming input, so correct those errors and retry the
same block. `FAAC_ERR_INTERNAL` may occur after state has advanced; do not
assume replay is safe. Close and reopen to start a new stream.
Flush is an end-of-stream drain, not a reset or seek operation. Submit the final
partial block first, then flush until zero output; close does not drain audio.

### Function reference

```c
/* library-global facts: compiled channel ceiling, version/copyright */
faac_status faac_get_library_info(faac_library_info *out);

faac_status faac_params_init(faac_params *p, uint32_t caller_size);  /* sizeof(*p) */
faac_status faac_encoder_open(const faac_params *p, faac_encoder **out);
faac_status faac_encoder_close(faac_encoder **enc);   /* sets *enc = NULL */

/* resolved properties, valid after open; set out.struct_size first */
faac_status faac_encoder_get_info(faac_encoder *enc, faac_encoder_info *out);

/* AudioSpecificConfig: library-owned, valid until close, do NOT free */
faac_status faac_encoder_asc(faac_encoder *enc, const uint8_t **buf, uint32_t *len);

faac_status faac_encoder_encode(faac_encoder *enc,
                                const void *in, uint32_t in_samples,
                                uint8_t *out, uint32_t out_cap,
                                uint32_t *bytes_written);

const char *faac_strerror(faac_status status);
```

The `object_type` field of `faac_params` uses MPEG-4 Audio Object Type
numbers: `FAAC_OBJ_LOW` (2) is AAC-LC and `FAAC_OBJ_HE_AAC_V1` (5) is
HE-AAC v1 (AAC-LC core + SBR). `FAAC_OBJ_AUTO` (0) lets the library choose
LC or HE-AAC from the bitrate, or from `quant_quality` when there is none. `FAAC_OBJ_HE_AAC_V2` (29) is defined but not
implemented; requesting it returns `FAAC_ERR_UNSUPPORTED`.

For an HE-AAC encoder the SBR core runs at half the input rate and codes a
2048-sample frame, so `faac_encoder_get_info()` reports
`frame_samples` == 2048 and `sample_rate` == the full (un-halved)
output rate. Buffer sizing and rate reporting therefore stay correct without
the caller knowing any SBR internals.

### Complete ADTS encode example

Save as `adts_encode.c`. It reads headerless, native-endian, interleaved signed
16-bit stereo PCM at 48000 Hz from stdin and writes ADTS to stdout (128 kbit/s
total). It expects binary streams; on Windows, set stdin/stdout to binary mode.
WAV headers must be parsed and removed by the application.

```c
#include <faac.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    int16_t *pcm = NULL;
    uint8_t *out = NULL;
    faac_status st;
    int result = EXIT_FAILURE;

    st = faac_params_init(&p, sizeof(p));
    if (st < 0) goto api_error;
    p.sample_rate = 48000;
    p.num_channels = 2;
    p.bit_rate = 64000; /* per channel */
    st = faac_encoder_open(&p, &enc);
    if (st < 0) goto api_error;
    st = faac_encoder_get_info(enc, &info);
    if (st < 0) goto api_error;

    uint32_t capacity = info.frame_samples * p.num_channels;
    size_t input_bytes = (size_t)capacity * sizeof(*pcm);
    pcm = malloc(input_bytes);
    out = malloc(info.max_output_bytes);
    if (!pcm || !out) { fputs("allocation failed\n", stderr); goto cleanup; }

    for (;;) {
        size_t used = 0;
        /* Accumulate short reads so only the final block can be partial. */
        while (used < input_bytes) {
            size_t n = fread((uint8_t *)pcm + used, 1, input_bytes - used, stdin);
            used += n;
            if (n == 0) {
                if (ferror(stdin)) { perror("read"); goto cleanup; }
                break;
            }
        }
        if (used == 0) break;
        if (used % (sizeof(*pcm) * p.num_channels)) {
            fputs("incomplete PCM channel group\n", stderr);
            goto cleanup;
        }
        uint32_t written = 0;
        st = faac_encoder_encode(enc, pcm, (uint32_t)(used / sizeof(*pcm)),
                                 out, info.max_output_bytes, &written);
        if (st < 0) goto api_error;
        if (fwrite(out, 1, written, stdout) != written) {
            perror("write"); goto cleanup;
        }
    }
    for (;;) {
        uint32_t written = 0;
        st = faac_encoder_encode(enc, NULL, 0, out,
                                 info.max_output_bytes, &written);
        if (st < 0) goto api_error;
        if (written == 0) break;
        if (fwrite(out, 1, written, stdout) != written) {
            perror("write"); goto cleanup;
        }
    }
    if (fflush(stdout) != 0) { perror("write"); goto cleanup; }
    result = EXIT_SUCCESS;
    goto cleanup;

api_error:
    fprintf(stderr, "FAAC: %s\n", faac_strerror(st));
cleanup:
    free(out);
    free(pcm);
    faac_encoder_close(&enc);
    return result;
}
```

### Defaults and PCM input

`faac_params_init()` defaults to MPEG-4 AAC-LC, mixed joint stereo, TNS and
PNS enabled, LFE disabled, ADTS, signed 16-bit input, normal block switching,
64000 bits/sec/channel, automatic rate control, no user peak cap and automatic
bandwidth/quality. Only sample rate and channel count must be supplied.
`FAAC_OBJ_AUTO` is an explicit choice, not the initialization default.

For VBR, clear the default bitrate:

```c
p.bit_rate = 0;
p.rate_control = FAAC_RC_VBR;
p.quant_quality = 100; /* higher means higher quality; 0 selects the default */
```

Quality is clamped by the core to 1–5000; bandwidth is resolved and
clamped to the available range. Read effective values with `get_info()`.

| Input format | Representation | Storage alignment |
|---|---|---|
| `FAAC_INPUT_16BIT` | Native-endian signed `int16_t`, full scale | `int16_t` |
| `FAAC_INPUT_24BIT` | Native-endian packed signed three-byte samples, full scale | Any |
| `FAAC_INPUT_32BIT` | Native-endian `int32_t` containing sign-extended 24-bit values, not full-scale 32-bit PCM | `int32_t` |
| `FAAC_INPUT_FLOAT` | 32-bit float on the signed-16-bit scale (approximately −32768 to +32767) | `float` |

All formats are interleaved. Convert normalized float PCM by multiplying by
32768; convert full-scale 32-bit PCM to 24-bit scale before passing it.
NaN, infinity and float magnitudes at least 8388608 are replaced with silence;
this is not a clipping or normalization service.

### Channel ordering and ownership

Mono and stereo use mono and L/R order. Multichannel input follows AAC element
order, with the center first, channel pairs next and the final unpaired channel
last. For conventional 5.1 use C, L, R, Ls, Rs, LFE and set `use_lfe = true`.
For source order L, R, C, LFE, Ls, Rs:

```c
int32_t map[6] = { 2, 0, 1, 4, 5, 3 };
p.channel_map = map;
p.channel_map_count = 6;
p.use_lfe = true;
```

`channel_map[destination_channel]` selects the source PCM channel; entries must
be in `0..num_channels-1`, with at least `num_channels` entries supplied.
NULL selects identity order. Use a permutation to reorder channels; the map
does not mix or downmix audio. For the conventional six- and eight-channel layouts, LFE selection applies
to the trailing unpaired channel; it does not locate an LFE within the source layout automatically.

Open copies parameters and the map; their storage may be released after open.
Input and output buffers remain caller-owned; PCM is copied into internal
storage during encode. ASC remains library-owned until close; copy it if the
muxer needs it longer. Version/copyright and error strings are static and must
not be freed. Closing a NULL handle via `faac_encoder_close(&enc)` succeeds.
Independent handles may run concurrently; serialize all access to one handle.
The optional `stats` build prints diagnostics on close.

### Custom allocators

The encoder allocates its state on the heap through the `AllocMemory` /
`FreeMemory` macros in `libfaac/util.h`. A source build can override them, for
example with `-DAllocMemory=my_alloc -DFreeMemory=my_free` (provide the
function declarations when compiling). There is no caller-owned-state path, so the allocator is the only control
over where encoder memory lives. The allocator need not clear the memory it returns;
16-byte-aligned storage is sufficient for the current implementation.

### Memory budget and embedded targets

The encoder instance is heap-allocated at `faac_encoder_open()` and does not
allocate again while encoding or flushing. The first successful
`faac_encoder_asc()` call additionally caches 2 bytes for LC, 5 bytes for stereo
HE-AAC, or 7 bytes for mono HE-AAC. Closing the handle releases all of it.

The following are indicative measurements from an arm64 build at 48000 Hz
with default build options, not storage requirements guaranteed across builds:

| Configuration | Encoder heap | Frame samples/channel | 16-bit PCM input per call |
|---|---|---|---|
| Mono, AAC-LC | 154 KiB | 1024 | 2 KiB |
| Stereo, AAC-LC | 187 KiB | 1024 | 4 KiB |
| 5.1, AAC-LC | 319 KiB | 1024 | 12 KiB |
| 7.1, AAC-LC | 385 KiB | 1024 | 16 KiB |
| Mono, HE-AAC | 311 KiB | 2048 | 4 KiB |
| Stereo, HE-AAC | 353 KiB | 2048 | 8 KiB |

Add the output buffer, `info.max_output_bytes` (8 KiB in every configuration
above), and your input buffer. Shared tables and read-only data are separate
from the heap figures. Writable shared tables are initialized on first open
and remain in static storage. Measure static storage and stack requirements
on your target separately.

Use these figures for a first sizing, then measure your own build: sizes follow
pointer width and alignment, and build options such as `-Dmax-channels` may
change them. HE-AAC roughly doubles the heap because it runs a core at half
the sample rate plus the SBR analysis state, so choose `FAAC_OBJ_LOW` when RAM
is tight.
The core uses single-precision float; double precision appears only in
one-time table generation at open.

A port can direct dynamically allocated encoder state into a specific
region, for example external RAM, by overriding `AllocMemory` as
described above. Check the speed cost of that placement on your target before
committing to it.

#### Running without a heap

A fixed-pool override can avoid the system heap. Allocations occur during open
and the first successful ASC request; encoding and flushing allocate nothing.
A bump allocator must also reserve space for allocations freed during open,
because it cannot reuse them.

Force-include a header declaring `pool_alloc(size_t)` and `pool_free(void *)`
when compiling libfaac with `-DAllocMemory=pool_alloc -DFreeMemory=pool_free`.
Compile their definitions into your application:

```c
#include <stddef.h>

/* Define POOL_BYTES for your build and encoder configuration. */
static _Alignas(16) unsigned char pool[POOL_BYTES];
static size_t used;

void *pool_alloc(size_t n)
{
    if (n > sizeof(pool) - used)
        return NULL;
    n = (n + 15u) & ~(size_t)15u;
    if (n > sizeof(pool) - used)
        return NULL;
    void *p = pool + used;
    used += n;
    return p;
}

void pool_free(void *p) { (void)p; }
void pool_reset(void) { used = 0; }
```

Use one encoder at a time with this pool and serialize access. Measure `used`
after open and a successful ASC request to determine consumption on your
build. Reset only after close or a failed open. Insufficient storage returns
`FAAC_ERR_NO_MEMORY` from open or ASC, respectively. Shared tables, stack,
and caller-owned input and output buffers remain separate from the pool.

### Encoder delay and gapless output

Encoding includes priming and trailing padding. `info.encoder_delay` is the
priming count in samples/channel at the full output rate: use it verbatim for
gapless tags or an MP4 edit list. It is not necessarily `frame_samples` for
HE-AAC and excludes SBR decoder delay, which the decoder handles separately.

Track original PCM samples/channel and emitted access units, including flush
output. The coded duration is `frame_count * info.frame_samples`; trailing
padding is the excess over `info.encoder_delay + original_samples`, or zero
when there is no excess. Use 64-bit counters. The application/muxer writes
these values; ADTS alone does not preserve gapless metadata. See
`frontend/encode_engine.c` and `frontend/mp4write.c` for container integration.

### Rate control modes

`faac_params.rate_control` selects how a rate is held. `FAAC_RC_AUTO`,
the default, keeps the legacy meaning of the two rate fields: a `bit_rate`
is ABR, none is VBR. `FAAC_RC_ABR` holds `bit_rate` as an average:
frames are bounded only by the ISO/IEC 14496-3 ceiling of 6144 bits per channel,
a transient may take several times the mean, and silence stays small.
`FAAC_RC_CBR` holds it as a constant-rate stream: a bit reservoir models the
decoder's input buffer, every frame fits `mean + fill`, a frame that would
leave the buffer to overflow is stuffed up to it, and the rate lands exactly.
ADTS output carries the fill in every header's `buffer_fullness` (ABR and
VBR write the 0x7FF sentinel); MP4 output declares `bufferSizeDB` = 768
bytes per channel and `maxBitrate` == `avgBitrate`. CBR is for
constant-rate channels and matched-bitrate comparisons; on a file or a packet
network its stuffing is bytes for nothing, and a transient may take only what
the buffer holds. `FAAC_RC_VBR` or `FAAC_RC_ABR`/`FAAC_RC_CBR`
with, respectively, a `bit_rate` set or unset is rejected by
`faac_encoder_open()` with `FAAC_ERR_INVALID_ARGUMENT`.
`faac_encoder_info.rate_control` reports the resolved mode.

In ABR and CBR the cutoff follows `bit_rate`; in VBR it is the top of
that curve, 18750 Hz. A non-zero `bandwidth` overrides either. A non-zero `quant_quality` given
together with a `bit_rate` is not rejected: it is the rate loop's starting
quality, and the loop moves on from it. `faac_encoder_info.bandwidth`
reports the cutoff in use.

### Capping the peak frame size

Packet-oriented transports often cannot fragment a frame: one that overruns the
link MTU is dropped, not split. `bit_rate` cannot prevent that, being an
average individual frames are free to exceed — a transient easily produces
a frame several times the mean. `faac_params.max_bit_rate` puts a hard
ceiling on any single frame; 0, the default, means none beyond the encoder's
own ISO/IEC 14496-3 peak ceiling of 6144 bits per channel per frame, enforced
unconditionally to guarantee decoder compatibility across portable devices.
Unlike `bit_rate` it is a whole-stream rate, so it stays independent of
`num_channels` and follows directly from the payload one packet can carry:

```text
    max_bit_rate = payload_bytes * 8 * info.sample_rate / info.frame_samples
```

Use 64-bit arithmetic, round down, and query the resolved frame size/rate
first (1024 for LC, 2048 for HE-AAC). The cap budgets the AAC payload; reserve
seven more bytes for ADTS and any transport headers when deriving an MTU budget.
The output allocation still needs `info.max_output_bytes`, regardless of cap.

It must be at least `bit_rate` × `num_channels` — a peak
below the average is unsatisfiable — and at most
`FAAC_MAX_BIT_RATE`; `faac_encoder_open()` returns
`FAAC_ERR_INVALID_ARGUMENT` otherwise. More headroom means fewer retries.

It is a ceiling, not a second average: frames that already fit are untouched, so
the stream stays ABR rather than becoming CBR, and a frame re-quantized to meet
the cap does not drag down the frames after it. Enforcement is best-effort
— a frame cannot shrink below the cost of its own side info and
scalefactors, so a small enough cap is still exceeded on a few percent of frames
(measured on transient-heavy stereo, the cap holds everywhere at and above
roughly 64 kbit/s and starts to slip around 48). Callers that must guarantee the
packet fits should check `bytes_written` and drop the frame themselves.

## Porting from the legacy faacEnc* API

The classic `faacEnc*` surface (`faacEncOpen`,
`faacEncGetCurrentConfiguration`/`faacEncSetConfiguration`,
`faacEncEncode`, `faacEncClose`, `faacEncGetVersion`,
`faacEncGetDecoderSpecificInfo`, and the `faacEncConfiguration`
struct from `faaccfg.h`) has been removed. There is no compatibility
shim — callers must move to `faac_*`. The changes are mechanical:

- **Open sequence.** The old two-step open (`faacEncOpen()` to get a
  handle plus `inputSamples`/`maxOutputBytes` out-params, then
  `faacEncGetCurrentConfiguration()` / `faacEncSetConfiguration()` to
  apply options after the fact) collapses into one step: build a
  `faac_params`, fill in the fields you care about, and call
  `faac_encoder_open()` once. There is no live-pointer configuration
  struct to mutate after opening.
- **Buffer sizing.** Where you previously read `inputSamples` and
  `maxOutputBytes` from `faacEncOpen()`, call
  `faac_encoder_get_info()` after `faac_encoder_open()` and use
  `info.frame_samples` × `num_channels` and
  `info.max_output_bytes` instead.
- **Version info.** `faacEncGetVersion()` is replaced by
  `faac_get_library_info()`, which also reports `max_channels`; the
  removed `name`/`copyright` write-only config fields are gone along
  with it.
- **Decoder-specific info.** `faacEncGetDecoderSpecificInfo()` is
  replaced by `faac_encoder_asc()`. The legacy call allocated a caller-freed buffer; the new buffer is
  library-owned and valid until close. Remove the old `free()` call.
- **Encoding loop.** `faacEncEncode()` becomes
  `faac_encoder_encode()` with the byte count returned via an
  out-parameter (`bytes_written`) instead of the return value; the return
  value is now a `faac_status`. Flush and end-of-stream detection are
  unchanged: pass `in_samples` == 0 to flush, and keep calling until
  `bytes_written` is 0.
- **Close.** `faacEncClose(hEncoder)` becomes
  `faac_encoder_close(&enc)` — pass the address of your handle,
  not the handle itself; the library NULLs it on success.
- **Return values.** Where the old API returned NULL handles or -1 on
  error, every fallible `faac_*` call returns a `faac_status`; test
  `status < 0` and use `faac_strerror()` for a message.
- **Configuration field renames.** `faacEncConfiguration` fields map
  onto `faac_params` fields as follows:

  - `aacObjectType` → `object_type` (now uses
    `FAAC_OBJ_*` constants, numbered per the MPEG-4 AOT registry, rather
    than the old MAIN/LOW/SSR/LTP enum — MAIN, SSR, and LTP are retired,
    unimplemented object types).
  - `mpegVersion` → `mpeg_version` (`FAAC_MPEG4` /
    `FAAC_MPEG2`).
  - `jointmode` → `joint_mode` (`FAAC_JOINT_*`).
    `allowMidside` was a compatibility alias sharing storage with
    `jointmode` in a union; it has no separate replacement, use
    `joint_mode` directly.
  - `useLfe` → `use_lfe`; `useTns` → `use_tns`
    (now `bool`).
  - `shortctl` → `short_control` (`FAAC_SHORTCTL_*`).
  - `bitRate` → `bit_rate`; `bandWidth` →
    `bandwidth`; `quantqual` → `quant_quality`.
  - `pnslevel` → `use_pns`: the level is now chosen by the
    encoder, leaving the caller an on/off switch.
  - `outputFormat` → `output_format` (`FAAC_STREAM_*`);
    `inputFormat` → `input_format` (`FAAC_INPUT_*`).
  - `channel_map[64]` → `channel_map` plus an explicit
    `channel_map_count` (the old fixed-size array was implicitly sized by
    `numChannels`).
  - `version`, `name`, and `copyright` were unused or
    library-populated informational fields in the legacy config struct; their
    closest analogs are `faac_params.struct_size` (for `version`-style
    compatibility checks) and `faac_library_info.version`/`copyright`
    (queried via `faac_get_library_info()`), not a `faac_params` field.

- **Struct growth.** Always call `faac_params_init()` before setting
  any fields, and set `struct_size` on any struct you pass to a query
  function (`faac_encoder_get_info()`, `faac_get_library_info()`).
  This is how the ABI stays stable as these structs grow in later releases;
  the legacy config used a configuration-version field rather than a
  caller-sized, append-only layout.

## Porting from libfaac ABI 1

Project release 1.x used the legacy `faacEnc*` interface described above.
The earlier `faac_*` interface with library SONAME/ABI 1 is a separate migration:
ABI 2 added `caller_size` to `faac_params_init()`. Replace
`faac_params_init(&p)` with `faac_params_init(&p, sizeof(p))` and rebuild/link
against ABI 2. To compile source against either header:

```c
#if FAAC_VERSION_MAJOR >= 2
    st = faac_params_init(&p, sizeof(p));
#else
    st = faac_params_init(&p);
#endif
```

`FAAC_VERSION_MAJOR` identifies the library ABI, not the project release.
An ABI 1 binary must not load ABI 2 as a drop-in replacement. Do not cast
legacy configuration or handle types to the new public types; migrate the
calls and rebuild.

## ABI compatibility

Initialize parameters with `faac_params_init(&p, sizeof(p))`; initialize query
structs with `.struct_size = sizeof(info)`. Query functions update
`struct_size` to the number of bytes populated. Reinitialize it before reusing
a struct as a query destination. Check the returned populated size before
using an appended field when supporting older libraries.

The library accepts its frozen baseline struct sizes, ignores unknown trailing
configuration storage, and writes at most the caller's capacity in initialization
and queries. Bytes beyond the library's known layout remain untouched; callers
using newer structs with older libraries should also zero their own storage.
Named fields grow additively; do not repurpose reserved padding.

Fixed-width integers and width-pinned enums support `-fshort-enums` on a given
platform ABI. Pointer fields follow the target's pointer width and alignment;
layouts are not necessarily identical across 32-/64-bit targets. These structs
are not serializable file or network formats. Incompatible signature/layout
changes require a new library ABI version.

FAAC is free software, licensed under the GNU Lesser General Public License (LGPL), version 2.1 or later.

Copyright © 1999-2001, Menno Bakker ·
Copyright © 2002-2017, Krzysztof Nikiel ·
Copyright © 2004, Dan Villiom P. Christiansen ·
Copyright © 2005-2026, Fabian Greffrath ·
Copyright © 2026, Nils Schimmelmann
