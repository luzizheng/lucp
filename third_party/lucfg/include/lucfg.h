/*
 * lucfg.h
 * ════════════════════════════════════════════════════════════════════════
 * Extensible Configuration Parser (lucfg)
 * A thread-safe, lightweight, INI-style configuration parser for C/C++.
 * -----------------------------------------------------------------------
 * Author : Luzz <arloou@gmail.com>
 * License: MIT
 * ════════════════════════════════════════════════════════════════════════
 *
 * QUICK START
 * ┬─────────────────────────────────────────────────────────────────────
 * │ #include <lucfg.h>
 * │ lucfg_handle_t *h = lucfg_open("app.cfg");
 * │ const char *host;
 * │ if (lucfg_get_string(h, "network", "host", &host) == LUCFG_OK)
 * │     printf("host = %s\n", host);
 * │ lucfg_close(h);
 * └─────────────────────────────────────────────────────────────────────
 *
 * FEATURE SUMMARY
 * ┬─────────────────────────────────────────────────────────────────────
 * │ • Supports string, integer and floating-point values.
 * │ • Automatic trimming of white-space and optional double quotes.
 * │ • Section/key lookup is case-insensitive.
 * │ • Thread-safe: all public functions are mutex-protected.
 * │ • Zero dynamic allocation after parsing (except handle itself).
 * │ • Self-contained header: only requires <stdio.h> for FILE.
 * │ • Can be embedded or installed system-wide (CMake).
 * └─────────────────────────────────────────────────────────────────────
 *
 * CONFIGURATION FILE SYNTAX (INI-like)
 * ┬─────────────────────────────────────────────────────────────────────
 * │ # Comment line
 * │ [SectionName]
 * │ key1 = value               # string
 * │ key2 = 42                  # integer
 * │ key3 = 3.14159             # double
 * │ key4 = "hello world"       # quoted string (quotes stripped)
 * │
 * │ Rules
 * │ • Section and key names are case-insensitive.
 * │ • White-space around section, key and value is ignored.
 * │ • Everything after # or ; is treated as a comment.
 * │ • Values may be enclosed in double quotes; quotes are removed.
 * │ • If a value parses as a valid integer it is stored as long;
 * │   otherwise if it parses as a valid float it is stored as double;
 * │   otherwise it remains a string.
 * └─────────────────────────────────────────────────────────────────────
 *
 * ERROR HANDLING
 * ┬─────────────────────────────────────────────────────────────────────
 * │ All get-functions return an error code:
 * │   LUCFG_OK          ( 0)  success
 * │   LUCFG_ERR_OPEN    (-1)  cannot open file
 * │   LUCFG_ERR_PARSE   (-2)  syntax error in file
 * │   LUCFG_ERR_NOKEY   (-3)  section/key not found
 * │   LUCFG_ERR_TYPE    (-4)  value exists but type mismatch
 * │   LUCFG_ERR_LOCK    (-5)  internal mutex error (very rare)
 * └─────────────────────────────────────────────────────────────────────
 *
 * THREAD SAFETY
 * ┬─────────────────────────────────────────────────────────────────────
 * │ • A single lucfg_handle_t may be shared among multiple threads.
 * │ • All read operations are protected by an internal mutex.
 * │ • The returned pointers (const char *) remain valid until
 * │   lucfg_close() is called or the handle is reloaded.
 * │ • DO NOT free the returned string pointers.
 * └─────────────────────────────────────────────────────────────────────
 *
 * MEMORY MANAGEMENT
 * ┬─────────────────────────────────────────────────────────────────────
 * │ • lucfg_open()  allocates and returns a handle.
 * │ • lucfg_close() destroys the handle and frees all resources.
 * │ • Returned values (const char *) are owned by the handle;
 * │   do not free them manually.
 * └─────────────────────────────────────────────────────────────────────
 */

#ifndef LUCFG_H_
#define LUCFG_H_

#include <stdio.h>   /* FILE */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Forward declaration of the opaque handle type.                     */
/* ------------------------------------------------------------------ */
typedef struct lucfg_handle lucfg_handle_t;

/* ------------------------------------------------------------------ */
/* Error codes returned by all get-functions.                         */
/* ------------------------------------------------------------------ */
#define LUCFG_OK          0   /* Success                                      */
#define LUCFG_ERR_OPEN   -1   /* Cannot open configuration file               */
#define LUCFG_ERR_PARSE  -2   /* Syntax error while parsing                   */
#define LUCFG_ERR_NOKEY  -3   /* Requested section/key pair not found         */
#define LUCFG_ERR_TYPE   -4   /* Value exists but requested type is incorrect */
#define LUCFG_ERR_LOCK   -5   /* Internal mutex lock failure (very rare)      */
#define LUCFG_ERR_RANGE  -6   /* 值超出目标类型范围 */

static inline const char *lucfg_errname(int rc)
{
    switch (rc) {
    case LUCFG_OK:        return "OK";
    case LUCFG_ERR_OPEN:  return "OPEN ERR";
    case LUCFG_ERR_PARSE: return "PARSE ERR";
    case LUCFG_ERR_NOKEY: return "NO KEY";
    case LUCFG_ERR_TYPE:  return "Err TYPE";
    case LUCFG_ERR_RANGE: return "RANGE ERR";
    default:             return "UNKNOWN ERR";
    }
}


/* ================================================================== */
/* LIFE-CYCLE FUNCTIONS                                               */
/* ================================================================== */

/**
 * lucfg_open()
 * --------------------------------------------------------------------
 * Parse the given configuration file and create a new handle.
 *
 * @param filename  Path to the INI-style configuration file.
 * @return         Valid handle on success, NULL on any error.
 *                 Errors include file-not-found or parse failure.
 *
 * Thread-safe: yes (mutex held only during parsing).
 */
lucfg_handle_t *lucfg_open(const char *filename);

/**
 * lucfg_close()
 * --------------------------------------------------------------------
 * Destroy a handle obtained from lucfg_open().
 * All returned pointers from this handle become invalid.
 *
 * @param h  Handle to destroy. NULL is silently ignored.
 *
 * Thread-safe: yes (mutex held during destruction).
 */
void lucfg_close(lucfg_handle_t *h);

/* ================================================================== */
/* VALUE RETRIEVAL FUNCTIONS                                          */
/* ================================================================== */

/**
 * lucfg_get_string()
 * --------------------------------------------------------------------
 * Retrieve a value as a null-terminated C string.
 *
 * @param h         Valid handle.
 * @param section   Section name (case-insensitive).
 * @param key       Key name (case-insensitive).
 * @param out_value Output: pointer to the string (owned by handle).
 * @return          LUCFG_OK on success, otherwise an error code.
 *
 * Thread-safe: yes.
 */
int lucfg_get_string(lucfg_handle_t *h,
                    const char *section,
                    const char *key,
                    const char **out_value);

/**
 * lucfg_get_int()
 * --------------------------------------------------------------------
 * Retrieve a value as a signed long integer.
 *
 * @param h         Valid handle.
 * @param section   Section name (case-insensitive).
 * @param key       Key name (case-insensitive).
 * @param out_value Output: integer value.
 * @return          LUCFG_OK if the value was parsed as an integer,
 *                  LUCFG_ERR_TYPE if the value is not an integer,
 *                  LUCFG_ERR_NOKEY if the key does not exist.
 *
 * Thread-safe: yes.
 */
int lucfg_get_int(lucfg_handle_t *h,
                 const char *section,
                 const char *key,
                 long *out_value);

/**
 * lucfg_get_double()
 * --------------------------------------------------------------------
 * Retrieve a value as a double-precision floating point number.
 *
 * @param h         Valid handle.
 * @param section   Section name (case-insensitive).
 * @param key       Key name (case-insensitive).
 * @param out_value Output: double value.
 * @return          LUCFG_OK if the value was parsed as a double,
 *                  LUCFG_ERR_TYPE if the value is not a double,
 *                  LUCFG_ERR_NOKEY if the key does not exist.
 *
 * Thread-safe: yes.
 */
int lucfg_get_double(lucfg_handle_t *h,
                    const char *section,
                    const char *key,
                    double *out_value);


/* ---------------- 窄类型安全接口 ---------------- */
int lucfg_get_bool    (lucfg_handle_t *h, const char *sec, const char *key, int *out);      /* C99 bool  0/1 */

int lucfg_get_int8    (lucfg_handle_t *h, const char *sec, const char *key, int8_t  *out);
int lucfg_get_uint8   (lucfg_handle_t *h, const char *sec, const char *key, uint8_t *out);

int lucfg_get_int16   (lucfg_handle_t *h, const char *sec, const char *key, int16_t *out);
int lucfg_get_uint16  (lucfg_handle_t *h, const char *sec, const char *key, uint16_t *out);

int lucfg_get_int32   (lucfg_handle_t *h, const char *sec, const char *key, int32_t *out);
int lucfg_get_uint32  (lucfg_handle_t *h, const char *sec, const char *key, uint32_t *out);

int lucfg_get_int64   (lucfg_handle_t *h, const char *sec, const char *key, int64_t *out);
int lucfg_get_uint64  (lucfg_handle_t *h, const char *sec, const char *key, uint64_t *out);

/* ================================================================== */
/* DEBUG / UTILITY FUNCTIONS                                          */
/* ================================================================== */

/**
 * lucfg_dump()
 * --------------------------------------------------------------------
 * Print the entire configuration to a FILE stream (e.g. stdout).
 * Useful for debugging.
 *
 * @param h  Valid handle.
 * @param fp Open FILE stream (e.g. stdout, stderr, fopen() result).
 *
 * Thread-safe: yes (mutex held during iteration).
 */
void lucfg_dump(lucfg_handle_t *h, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif /* LUCFG_H_ */