/*
 * test_lucfg Straight.c  —— 无宏、直观、全覆盖
 * gcc -I./include -L./build -o testStraight test_lucfg Straight.c -llucfg -Wl,-rpath,./build
 */
#include <stdio.h>
#include <stdint.h>
#include <lucfg.h>

/* 老编译器兜底 */
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L)
# define PRId8  "d"
# define PRIu8  "u"
# define PRId16 "d"
# define PRIu16 "u"
# define PRId32 "d"
# define PRIu32 "u"
# define PRId64 "lld"
# define PRIu64 "llu"
#endif



static void test_expressions(lucfg_handle_t *h)
{
    const char *sec = "expressions";
    const char *sv;
    int32_t i32;
    double  dv;
    int rc;

    rc = lucfg_get_string(h, sec, "prop_expr", &sv);
    printf("[%s:%-15s] string  \"%s\"  (%s)\n", sec, "prop_expr", rc ? lucfg_errname(rc) : sv, lucfg_errname(rc));

    rc = lucfg_get_int32(h, sec, "prop_expr", &i32);
    printf("[%s:%-15s] int32   %d  (%s)\n", sec, "prop_expr", rc ? 0 : i32, lucfg_errname(rc));

    rc = lucfg_get_double(h, sec, "prop_expr", &dv);
    printf("[%s:%-15s] double  %.6f  (%s)\n", sec, "prop_expr", rc ? 0.0 : dv, lucfg_errname(rc));
}

/* 把原来所有 PRI 宏换成普通格式串 */
static void test_limits(lucfg_handle_t *h)
{
    const char *sec = "limits";
    int rc;
    uint8_t  u8;  int8_t  i8;
    uint16_t u16;
    // int16_t i16;
    int      b;

    /* uint8 / int8 用 %d / %u （char 提升为 int）*/
    rc = lucfg_get_uint8(h, sec, "u8_max", &u8);
    printf("[%s:%-15s] uint8   %u  (%s)\n", sec, "u8_max", rc ? 0 : u8, lucfg_errname(rc));

    rc = lucfg_get_uint8(h, sec, "u8_max_p1", &u8);
    printf("[%s:%-15s] uint8   ---  (%s)\n", sec, "u8_max_p1", lucfg_errname(rc));

    rc = lucfg_get_int8(h, sec, "i8_min", &i8);
    printf("[%s:%-15s] int8    %d  (%s)\n", sec, "i8_min", rc ? 0 : i8, lucfg_errname(rc));

    rc = lucfg_get_int8(h, sec, "i8_min_m1", &i8);
    printf("[%s:%-15s] int8    ---  (%s)\n", sec, "i8_min_m1", lucfg_errname(rc));

    /* uint16 / int16 用 %u / %d */
    rc = lucfg_get_uint16(h, sec, "u16_max", &u16);
    printf("[%s:%-15s] uint16  %u  (%s)\n", sec, "u16_max", rc ? 0 : u16, lucfg_errname(rc));

    /* bool 用 %s 打印 true/false */
    const char *bool_keys[] = {"bool_true","bool_false","bool_01","bool_00","bool_bad"};
    for (size_t i = 0; i < sizeof(bool_keys)/sizeof(bool_keys[0]); ++i) {
        rc = lucfg_get_bool(h, sec, bool_keys[i], &b);
        printf("[%s:%-15s] bool    %s  (%s)\n", sec, bool_keys[i],
               rc ? "---" : (b ? "true" : "false"), lucfg_errname(rc));
    }
}



static void test_strings(lucfg_handle_t *h)
{
    const char *sec = "strings";
    const char *sv;
    int rc;

    rc = lucfg_get_string(h, sec, "str_plain", &sv);
    printf("[%s:%-15s] string  \"%s\"  (%s)\n", sec, "str_plain", rc ? lucfg_errname(rc) : sv, lucfg_errname(rc));

    rc = lucfg_get_string(h, sec, "str_quoted", &sv);
    printf("[%s:%-15s] string  \"%s\"  (%s)\n", sec, "str_quoted", rc ? lucfg_errname(rc) : sv, lucfg_errname(rc));

    rc = lucfg_get_string(h, sec, "str_number", &sv);
    printf("[%s:%-15s] string  \"%s\"  (%s)\n", sec, "str_number", rc ? lucfg_errname(rc) : sv, lucfg_errname(rc));
}

static void test_errors(lucfg_handle_t *h)
{
    const char *sec = "errors";
    uint64_t u64;
    const char *sv;
    int rc;

    rc = lucfg_get_uint64(h, sec, "bad_range", &u64);
    printf("[%s:%-15s] uint64  ---  (%s)\n", sec, "bad_range", lucfg_errname(rc));

    rc = lucfg_get_string(h, sec, "not_exist", &sv);
    printf("[%s:%-15s] string  ---  (%s)\n", sec, "not_exist", lucfg_errname(rc));
}

/* ---------------- 主入口 ---------------- */
int main(void)
{
    lucfg_handle_t *h = lucfg_open("/etc/test_lucfg.cfg");
    if (!h) { perror("lucfg_open"); return 1; }

    puts("=== [expressions] ===");
    test_expressions(h);

    puts("\n=== [limits] 边界与范围 ===");
    test_limits(h);

    puts("\n=== [strings] ===");
    test_strings(h);

    puts("\n=== [errors] ===");
    test_errors(h);

    lucfg_close(h);
    puts("\nAll tests finished.");
    return 0;
}