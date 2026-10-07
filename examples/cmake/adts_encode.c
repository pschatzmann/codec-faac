/* Extracted from docs/libfaac.md by create_arduino_lib.py */
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
