/*
******************************************************************************
**
** @file        : STM32N6XX_LRUN_ns.ld
**
** @author      : GPM Application Team
**
** @brief       : Linker script for STM32N6XX Device from STM32N6 series
**                      512 KBytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed as is, without any warranty
**                of any kind.
**
******************************************************************************
** @attention
**
** Copyright (c) 2026 STMicroelectronics.
** All rights reserved.
**
** This software is licensed under terms that can be found in the LICENSE file
** in the root directory of this software component.
** If no LICENSE file comes with this software, it is provided AS-IS.
**
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM); /* end of "RAM" Ram type memory */
_sstack = _estack - _Min_Stack_Size;

_Min_Heap_Size = 0x200; /* required amount of heap */
/* MSP stack until the kernel starts; MSPLIM is set to _sstack */
_Min_Stack_Size = 0x4000; /* 16 KB */

/* ROM:      lower half of AXISRAM2, opened to non-secure by the FSBL.
 *           The upper half holds the running FSBL.
 * RAM:      first 400K of AXISRAM1.
 * AXISRAM1: the rest of the bank, non-cacheable (MPU region 0). The bank
 *           ends at 0x2409BFFF; above that, writes are silently dropped. */
MEMORY
{
  ROM    (xrw)    : ORIGIN = 0x24100400,   LENGTH = 511K
  RAM    (xrw)    : ORIGIN = 0x24000000,   LENGTH = 400K
  AXISRAM1 (xrw)  : ORIGIN = 0x24064000,   LENGTH = 224K
}

/* Sections */
SECTIONS
{
  /* The startup code into "ROM" Rom type memory */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >ROM

  /* The program code and other data into "ROM" Ram type memory */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >ROM

  /* Constant data into "ROM" Rom type memory */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >ROM

  .ARM.extab   : {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >ROM

  .ARM : {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >ROM

  .preinit_array     :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >ROM

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >ROM

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >ROM

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections into "RAM" Ram type memory */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */

  } >RAM AT> ROM

  .noncacheable :
  {
    . = ALIGN(8);
    __snoncacheable = .;/* create symbol for start of section */
    KEEP(*(.noncacheable))
    . = ALIGN(8);
    __enoncacheable = .;  /* create symbol for end of section */
  } >AXISRAM1   /* HAL __NON_CACHEABLE objects, e.g. DMA linked-list nodes */

  /* audio DMA buffers */
  .non_cacheable_data :
  {
    . = ALIGN(32);
    *(.non_cacheable_data)
    . = ALIGN(32);
  } >AXISRAM1

  /* large buffers, not zeroed at startup */
  .axisram1 (NOLOAD) :
  {
    . = ALIGN(32);
    *(.axisram1)
    *(.axisram1*)
    . = ALIGN(32);
  } >AXISRAM1

  /* Uninitialized data section into "RAM" Ram type memory */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM

  /* micro T-Kernel: RAM vector table (1 KB aligned) and system memory */
  .mtk_exctbl (NOLOAD) :
  {
    . = ALIGN(1024);
    __mtk_exctbl_start = .;   /* non-cacheable, see MPU_Config */
    KEEP(*(.mtk_exctbl))
    . = ALIGN(32);
    __mtk_exctbl_end = .;
  } >RAM

  .mtk_sysmem (NOLOAD) :
  {
    . = ALIGN(8);
    *(.mtk_sysmem)
    . = ALIGN(8);
  } >RAM

  /* User_heap_stack section, used to check that there is enough "RAM" Ram  type memory left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* Remove information from the compiler libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
