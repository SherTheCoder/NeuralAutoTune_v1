/*
 * swiftf0_npu.h - pitch tracker: front-end, NPU inference, decode
 *
 * sf0_npu_push takes the same 16 kHz samples as the voice path. Every 10 ms
 * the audio task runs the front-end (sf0_npu_service) and the npu task runs
 * the network (sf0_npu_npu_service).
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SWIFTF0_NPU_H
#define SWIFTF0_NPU_H

#include <stdint.h>

#define SF0_HOP_INFER  160u   /* 10 ms */

typedef struct {
    float f0_hz;
    float confidence;   /* sigmoid of the voicing output */
    float voicing;      /* probability mass near the peak */
    float entropy;      /* 0..1 */
    uint32_t count;     /* inferences since boot */
    uint32_t cyc_fe;    /* front-end cycles */
    uint32_t cyc_npu;
    uint32_t max_pass;
    /* debug: int8 ranges of input and output, weight readback */
    int16_t  dbg_in_min, dbg_in_max;
    int16_t  dbg_pit_min, dbg_pit_max;
    uint8_t  dbg_w_rb, dbg_w_exp;
} sf0_result_t;

void sf0_npu_init(void);

/* decim16 in [-1, 1); only copies into the ring */
void sf0_npu_push(const float *decim16, uint32_t n);

void sf0_npu_service(void);		/* audio task: front-end when due */
uint8_t sf0_npu_npu_service(void);	/* npu task; returns 1 when an inference finished */

void sf0_npu_get(sf0_result_t *out);

/* 1 while running on the NPU; the RSN must wait (shared activation memory) */
uint8_t sf0_npu_busy(void);

uint32_t sf0_npu_wall_max(uint8_t reset);
int  sf0_npu_format(char *buf, uint32_t buflen);

/* C1 capture: the newest 1024 samples, oldest first */
void sf0_npu_c1_ring(int16_t *dst1024);

#endif /* SWIFTF0_NPU_H */
