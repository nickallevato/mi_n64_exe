#ifndef ULTRA64_H
#define ULTRA64_H

/* Minimal libultra types for the decompiled game code (MIPS, o32). Add types here as decompiled code needs them. */

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long long s64;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;

typedef u32 size_t;

#define NULL ((void*)0)
#define TRUE 1
#define FALSE 0

#endif
