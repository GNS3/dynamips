
#ifndef __GDB_UTILS__
#define __GDB_UITLS__

//#include "utils.h"

#define PUTCHAR(c,d)    (*(c->putchar))(d, c->out)
#define GETCHAR(c)      (*(c->getchar))(c->in)
#define FLUSH(c)        (*(c->flush))(c->out)

#ifndef BOOLEAN
#define BOOLEAN
typedef unsigned char boolean;
#endif

#ifndef DEBUG_CONTEXT
#define DEBUG_CONTEXT
typedef struct debug_context gdb_debug_context_t;
#endif

#ifndef GDB_SERVER_CONN
#define GDB_SERVER_CONN
typedef struct gdb_server_conn gdb_server_conn_t;
#endif

/* Common types */
#ifndef COMMON_TYPES
#define COMMON_TYPES
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
#endif

#ifndef CPU_GEN_T
#define CPU_GEN_T
typedef struct cpu_gen cpu_gen_t;
#endif

#endif
