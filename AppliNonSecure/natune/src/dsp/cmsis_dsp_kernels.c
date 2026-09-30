/*
 * cmsis_dsp_kernels.c - the CMSIS-DSP kernels this project uses, built as
 * one unit instead of linking the whole library. With MVE enabled each
 * kernel compiles its Helium path.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */

/* 48 -> 16 kHz decimation */
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_init_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_fir_decimate_f32.c"

/* HF path high-pass */
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_fir_init_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_fir_f32.c"

/* 16 -> 48 kHz interpolation, and the CMSIS lattice (benchmark only) */
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_init_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_iir_lattice_init_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_iir_lattice_f32.c"

/* real FFT */
#include "../../../../Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c"
#include "../../../../Drivers/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c"

#include "../../../../Drivers/CMSIS/DSP/Source/CommonTables/arm_common_tables.c"
#include "../../../../Drivers/CMSIS/DSP/Source/CommonTables/arm_const_structs.c"
#include "../../../../Drivers/CMSIS/DSP/Source/CommonTables/arm_mve_tables.c"

#include "../../../../Drivers/CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_mag_squared_f32.c"
