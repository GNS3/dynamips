/** @file
 * @brief Common includes, types, defines and platform specific stuff.
 *
 * This header should be included before other headers.
 * This header should not contain code.
 */

/*
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 * Copyright (c) 2014 Flávio J. Saraiva <flaviojs2005@gmail.com>
 */

#pragma once
#ifndef __DYNAMIPS_COMMON_H__
#define __DYNAMIPS_COMMON_H__

/* Config file - not used at the moment */
#if HAVE_DYNAMIPS_CONFIG_H
#include "dynamips_config.h"
#endif

/* By default, Cygwin supports only 64 FDs with select()! */
#if defined(__CYGWIN__) && !defined(FD_SETSIZE)
#define FD_SETSIZE 1024
#endif

#define _GNU_SOURCE
#include <stdarg.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <pthread.h>

/* True/False definitions */
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE  1
#endif

/* Endianness */
#define ARCH_BIG_ENDIAN     0x4321
#define ARCH_LITTLE_ENDIAN  0x1234

#if defined(PPC) || defined(__powerpc__) || defined(__ppc__)
#define ARCH_BYTE_ORDER ARCH_BIG_ENDIAN
#elif defined(__sparc) || defined(__sparc__)
#define ARCH_BYTE_ORDER ARCH_BIG_ENDIAN
#elif defined(__alpha) || defined(__alpha__)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#elif defined(__i386) || defined(__i386__) || defined(i386)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#define ARCH_REGPARM_SUPPORTED  1
#elif defined(__x86_64__)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#elif defined(__ia64__)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#elif defined(__arm__) || defined (__aarch64__)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#endif

#ifndef ARCH_BYTE_ORDER
#error Please define your architecture!
#endif

/* Useful attributes for functions */
#ifdef ARCH_REGPARM_SUPPORTED
#define asmlinkage __attribute__((regparm(0)))
#define fastcall   __attribute__((regparm(3)))
#else
#define asmlinkage
#define fastcall
#endif

#ifndef _unused
/* Function that is never used */
#define _unused  __attribute__((unused))
#endif

#ifndef _maybe_used
/* Function that is referenced from excluded code (commented out or depends on preprocessor) */
#define _maybe_used  __attribute__((unused))
#endif

#ifndef UNUSED
/* Variable that is never used (name is changed to get an error on use) */
#define UNUSED(x)  UNUSED_ ## x __attribute__((unused))
#endif

#if __GNUC__ > 2
#define forced_inline inline __attribute__((always_inline))
#define no_inline __attribute__ ((noinline))
#else
#define forced_inline inline
#define no_inline
#endif

#if __GNUC__ > 2
/* http://kerneltrap.org/node/4705 */
#define likely(x)    __builtin_expect(!!(x),1)
#define unlikely(x)  __builtin_expect((x),0)
#else
#define likely(x)    (x)
#define unlikely(x)  (x)
#endif

#ifndef _not_aligned
#define _not_aligned __attribute__ ((aligned (1)))
#endif

/* Common types */
typedef unsigned char m_uint8_t;
typedef signed char m_int8_t;

typedef unsigned short m_uint16_t;
typedef signed short m_int16_t;

typedef unsigned int m_uint32_t;
typedef signed int m_int32_t;

typedef unsigned long long m_uint64_t;
typedef signed long long m_int64_t;

typedef unsigned long m_iptr_t;
typedef m_uint64_t m_tmcnt_t;

/* Max and min macro */
#define m_max(a,b) (((a) > (b)) ? (a) : (b))
#define m_min(a,b) (((a) < (b)) ? (a) : (b))

/* A simple macro for adjusting pointers */
#define PTR_ADJUST(type,ptr,size) (type)((char *)(ptr) + (size))

/* Size of a field in a structure */
#define SIZEOF(st,field) (sizeof(((st *)NULL)->field))

/* Compute offset of a field in a structure */
#define OFFSET(st,f)     ((long)&((st *)(NULL))->f)

/* Stringify a constant */
#define XSTRINGIFY(val)  #val
#define STRINGIFY(val)   XSTRINGIFY(val)

#endif
