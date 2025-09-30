/*
 * lucfg.c
 * Extensible Configuration Parser (lucfg)
 * Author: Luzz <arloou@gmail.com>  MIT License
 */
#define _GNU_SOURCE
#include "../include/lucfg.h"
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HASH_SIZE 127

/* ---------------- 哈希表节点 ---------------- */
typedef struct entry
{
    char* section;
    char* key;
    char* value;       /* 原始字符串，永不覆盖 */
    long ivalue;       /* 当 type==1 */
    double dvalue;     /* 当 type==2 且 !has_expr */
    double expr_value; /* 表达式计算结果 */
    int type;          /* 0=str 1=int 2=double */
    int has_expr;      /* 1 表示 expr_value 有效 */
    struct entry* next;
} entry_t;

struct lucfg_handle
{
    entry_t* table[HASH_SIZE];
    pthread_mutex_t lock;
};

/* ---------------- 工具函数 ---------------- */
static unsigned hash_func(const char* sec, const char* key)
{
    unsigned h = 5381;
    int c;
    while ((c = *sec++))
        h = ((h << 5) + h) + c;
    while ((c = *key++))
        h = ((h << 5) + h) + c;
    return h % HASH_SIZE;
}
static int str_icmp(const char* a, const char* b)
{
    for (;; ++a, ++b)
    {
        int d = tolower((unsigned char) *a) - tolower((unsigned char) *b);
        if (d || *a == 0)
            return d;
    }
}
static char* trim(char* s)
{
    while (isspace((unsigned char) *s))
        ++s;
    if (*s == 0)
        return s;
    char* e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char) *e))
        --e;
    e[1] = 0;
    return s;
}
static void strip_comment(char* s)
{
    char* p    = s;
    int in_quo = 0;
    while (*p)
    {
        if (*p == '"')
            in_quo ^= 1;
        if (!in_quo && (*p == '#' || *p == ';'))
        {
            *p = 0;
            break;
        }
        ++p;
    }
}

/* ---------------- 微型表达式解析器 ---------------- */
typedef struct
{
    const char* s;
    const char* end;
} expr_t;
static double expr_parse(expr_t* e);
static void expr_skip_ws(expr_t* e)
{
    while (e->s < e->end && isspace((unsigned char) *e->s))
        ++e->s;
}
static double expr_atom(expr_t* e)
{
    expr_skip_ws(e);
    double sign = 1.0;
    if (*e->s == '-')
    {
        sign = -1.0;
        ++e->s;
    }
    if (*e->s == '(')
    {
        ++e->s;
        double v = expr_parse(e);
        expr_skip_ws(e);
        if (e->s < e->end && *e->s == ')')
            ++e->s;
        return sign * v;
    }
    char* next;
    double v = strtod(e->s, &next);
    if (next == e->s)
    {
        e->s = e->end;
        return NAN;
    }
    e->s = next;
    return sign * v;
}
static double expr_term(expr_t* e)
{
    double left = expr_atom(e);
    for (;;)
    {
        expr_skip_ws(e);
        if (e->s >= e->end)
            break;
        char op = *e->s;
        if (op != '*' && op != '/')
            break;
        ++e->s;
        double right = expr_atom(e);
        if (op == '*')
            left *= right;
        else
            left /= right;
    }
    return left;
}
static double expr_parse(expr_t* e)
{
    double left = expr_term(e);
    for (;;)
    {
        expr_skip_ws(e);
        if (e->s >= e->end)
            break;
        char op = *e->s;
        if (op != '+' && op != '-')
            break;
        ++e->s;
        double right = expr_term(e);
        if (op == '+')
            left += right;
        else
            left -= right;
    }
    return left;
}
static int try_eval_expr(const char* str, double* result)
{
    expr_t e = {.s = str, .end = str + strlen(str)};
    double v = expr_parse(&e);
    expr_skip_ws(&e);
    if (e.s != e.end || isnan(v))
        return 0;
    *result = v;
    return 1;
}

/* ---------------- parse_value ---------------- */
static void parse_value(entry_t* ent)
{
    char* s    = ent->value;
    s          = trim(s);
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"')
    {
        s[len - 1] = 0;
        ++s;
        free(ent->value);
        ent->value = strdup(s);
        if (!ent->value)
            return;
        s = ent->value;
    }
    /* 1. 尝试表达式 */
    double ev;
    if (try_eval_expr(s, &ev))
    {
        ent->expr_value = ev;
        ent->has_expr   = 1;
        ent->type       = 2;
        return;
    }
    ent->has_expr = 0;
    /* 2. 原整数 */
    char* end;
    long lv = strtol(s, &end, 10);
    if (*end == 0)
    {
        ent->type   = 1;
        ent->ivalue = lv;
        return;
    }
    /* 3. 原 double */
    double dv = strtod(s, &end);
    if (*end == 0)
    {
        ent->type   = 2;
        ent->dvalue = dv;
        return;
    }
    /* 4. 字符串 */
    ent->type = 0;
}

/* ---------------- load_file ---------------- */
static int load_file(lucfg_handle_t* h, const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp)
        return LUCFG_ERR_OPEN;
    char line[1024];
    char cur_sec[128] = "";
    entry_t* ent      = NULL;
    while (fgets(line, sizeof(line), fp))
    {
        if (line[sizeof(line) - 2] != '\0' && line[sizeof(line) - 2] != '\n')
        {
            fclose(fp);
            return LUCFG_ERR_PARSE;
        }
        strip_comment(line);
        char* p = trim(line);
        if (*p == 0)
            continue;
        if (*p == '[')
        {
            char* q = strchr(p, ']');
            if (!q)
            {
                fclose(fp);
                return LUCFG_ERR_PARSE;
            }
            *q = 0;
            strncpy(cur_sec, p + 1, sizeof(cur_sec) - 1);
            cur_sec[sizeof(cur_sec) - 1] = 0;
            continue;
        }
        char* eq = strchr(p, '=');
        if (!eq)
        {
            fclose(fp);
            return LUCFG_ERR_PARSE;
        }
        *eq       = 0;
        char* key = trim(p);
        char* val = trim(eq + 1);
        if (!*key || !*val)
        {
            fclose(fp);
            return LUCFG_ERR_PARSE;
        }
        ent = calloc(1, sizeof(*ent));
        if (!ent)
            goto cleanup;
        ent->section = strdup(cur_sec);
        ent->key     = strdup(key);
        ent->value   = strdup(val);
        if (!ent->section || !ent->key || !ent->value)
            goto cleanup;
        parse_value(ent);
        unsigned idx  = hash_func(cur_sec, key);
        ent->next     = h->table[idx];
        h->table[idx] = ent;
        ent           = NULL;
    }
    fclose(fp);
    return LUCFG_OK;
cleanup:
    if (ent)
    {
        free(ent->section);
        free(ent->key);
        free(ent->value);
        free(ent);
    }
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        entry_t* n = h->table[i];
        while (n)
        {
            entry_t* tmp = n->next;
            free(n->section);
            free(n->key);
            free(n->value);
            free(n);
            n = tmp;
        }
        h->table[i] = NULL;
    }
    fclose(fp);
    return LUCFG_ERR_OPEN;
}

/* ---------------- 对外接口 ---------------- */
lucfg_handle_t* lucfg_open(const char* filename)
{
    lucfg_handle_t* h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    pthread_mutex_init(&h->lock, NULL);
    pthread_mutex_lock(&h->lock);
    int rc = load_file(h, filename);
    pthread_mutex_unlock(&h->lock);
    if (rc != LUCFG_OK)
    {
        lucfg_close(h);
        return NULL;
    }
    return h;
}
void lucfg_close(lucfg_handle_t* h)
{
    if (!h)
        return;
    pthread_mutex_lock(&h->lock);
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        entry_t* n = h->table[i];
        while (n)
        {
            entry_t* tmp = n->next;
            free(n->section);
            free(n->key);
            free(n->value);
            free(n);
            n = tmp;
        }
    }
    pthread_mutex_unlock(&h->lock);
    pthread_mutex_destroy(&h->lock);
    free(h);
}
static entry_t* find_entry(lucfg_handle_t* h, const char* sec, const char* key)
{
    unsigned idx = hash_func(sec, key);
    for (entry_t* e = h->table[idx]; e; e = e->next)
        if (str_icmp(e->section, sec) == 0 && str_icmp(e->key, key) == 0)
            return e;
    return NULL;
}
int lucfg_get_string(lucfg_handle_t* h, const char* sec, const char* key, const char** out)
{
    pthread_mutex_lock(&h->lock);
    entry_t* e = find_entry(h, sec, key);
    int rc     = e ? LUCFG_OK : LUCFG_ERR_NOKEY;
    if (rc == LUCFG_OK)
        *out = e->value;
    pthread_mutex_unlock(&h->lock);
    return rc;
}
int lucfg_get_int(lucfg_handle_t* h, const char* sec, const char* key, long* out)
{
    pthread_mutex_lock(&h->lock);
    entry_t* e = find_entry(h, sec, key);
    int rc;
    if (!e)
        rc = LUCFG_ERR_NOKEY;
    else if (e->has_expr)
    {
        *out = (long) e->expr_value;
        rc   = LUCFG_OK;
    }
    else if (e->type == 1)
    {
        *out = e->ivalue;
        rc   = LUCFG_OK;
    }
    else
        rc = LUCFG_ERR_TYPE;
    pthread_mutex_unlock(&h->lock);
    return rc;
}
int lucfg_get_double(lucfg_handle_t* h, const char* sec, const char* key, double* out)
{
    pthread_mutex_lock(&h->lock);
    entry_t* e = find_entry(h, sec, key);
    int rc;
    if (!e)
        rc = LUCFG_ERR_NOKEY;
    else if (e->has_expr)
    {
        *out = e->expr_value;
        rc   = LUCFG_OK;
    }
    else if (e->type == 2)
    {
        *out = e->dvalue;
        rc   = LUCFG_OK;
    }
    else
        rc = LUCFG_ERR_TYPE;
    pthread_mutex_unlock(&h->lock);
    return rc;
}
void lucfg_dump(lucfg_handle_t* h, FILE* fp)
{
    pthread_mutex_lock(&h->lock);
    fprintf(fp, "---- lucfg dump ----\n");
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        for (entry_t* e = h->table[i]; e; e = e->next)
        {
            fprintf(fp, "[%s] %s = %s", e->section, e->key, e->value);
            if (e->has_expr)
                fprintf(fp, "  (expr=%.15g)", e->expr_value);
            else if (e->type == 1)
                fprintf(fp, "  (int)");
            else if (e->type == 2)
                fprintf(fp, "  (double)");
            fprintf(fp, "\n");
        }
    }
    pthread_mutex_unlock(&h->lock);
}

/* ---------------- 范围检查宏 ---------------- */
#define CHECK_RANGE_EXPR(min_v, max_v, target_type)                                                \
    do                                                                                             \
    {                                                                                              \
        if (e->has_expr)                                                                           \
        {                                                                                          \
            if (e->expr_value < (double) (min_v) || e->expr_value > (double) (max_v))              \
            {                                                                                      \
                rc = LUCFG_ERR_RANGE;                                                              \
                break;                                                                             \
            }                                                                                      \
            *out = (target_type) e->expr_value;                                                    \
            rc   = LUCFG_OK;                                                                       \
            break;                                                                                 \
        }                                                                                          \
    } while (0)

/* ---------------- 通用窄类型生成宏 ---------------- */
#define GEN_GET_SIGNED(name, T, MIN, MAX)                                                          \
    int name(lucfg_handle_t* h, const char* sec, const char* key, T* out)                          \
    {                                                                                              \
        pthread_mutex_lock(&h->lock);                                                              \
        entry_t* e = find_entry(h, sec, key);                                                      \
        int rc;                                                                                    \
        if (!e)                                                                                    \
        {                                                                                          \
            rc = LUCFG_ERR_NOKEY;                                                                  \
            goto end;                                                                              \
        }                                                                                          \
        do                                                                                         \
        {                                                                                          \
            CHECK_RANGE_EXPR(MIN, MAX, T);                                                         \
            if (e->type == 1)                                                                      \
            {                                                                                      \
                if (e->ivalue < (long long) (MIN) || e->ivalue > (long long) (MAX))                \
                {                                                                                  \
                    rc = LUCFG_ERR_RANGE;                                                          \
                    break;                                                                         \
                }                                                                                  \
                *out = (T) e->ivalue;                                                              \
                rc   = LUCFG_OK;                                                                   \
                break;                                                                             \
            }                                                                                      \
            rc = LUCFG_ERR_TYPE;                                                                   \
        } while (0);                                                                               \
    end:                                                                                           \
        pthread_mutex_unlock(&h->lock);                                                            \
        return rc;                                                                                 \
    }

#define GEN_GET_UNSIGNED(name, T, MAX)                                                             \
    int name(lucfg_handle_t* h, const char* sec, const char* key, T* out)                          \
    {                                                                                              \
        pthread_mutex_lock(&h->lock);                                                              \
        entry_t* e = find_entry(h, sec, key);                                                      \
        int rc;                                                                                    \
        if (!e)                                                                                    \
        {                                                                                          \
            rc = LUCFG_ERR_NOKEY;                                                                  \
            goto end;                                                                              \
        }                                                                                          \
        do                                                                                         \
        {                                                                                          \
            CHECK_RANGE_EXPR(0, MAX, T);                                                           \
            if (e->type == 1)                                                                      \
            {                                                                                      \
                if (e->ivalue < 0 || (uint64_t) e->ivalue > (MAX))                                 \
                {                                                                                  \
                    rc = LUCFG_ERR_RANGE;                                                          \
                    break;                                                                         \
                }                                                                                  \
                *out = (T) e->ivalue;                                                              \
                rc   = LUCFG_OK;                                                                   \
                break;                                                                             \
            }                                                                                      \
            rc = LUCFG_ERR_TYPE;                                                                   \
        } while (0);                                                                               \
    end:                                                                                           \
        pthread_mutex_unlock(&h->lock);                                                            \
        return rc;                                                                                 \
    }

/* ---------------- 一键生成所有窄类型 ---------------- */
GEN_GET_SIGNED(lucfg_get_int8, int8_t, INT8_MIN, INT8_MAX)
GEN_GET_UNSIGNED(lucfg_get_uint8, uint8_t, UINT8_MAX)

GEN_GET_SIGNED(lucfg_get_int16, int16_t, INT16_MIN, INT16_MAX)
GEN_GET_UNSIGNED(lucfg_get_uint16, uint16_t, UINT16_MAX)

GEN_GET_SIGNED(lucfg_get_int32, int32_t, INT32_MIN, INT32_MAX)
GEN_GET_UNSIGNED(lucfg_get_uint32, uint32_t, UINT32_MAX)

GEN_GET_SIGNED(lucfg_get_int64, int64_t, INT64_MIN, INT64_MAX)
GEN_GET_UNSIGNED(lucfg_get_uint64, uint64_t, UINT64_MAX)

/* ---------------- bool 专用 ---------------- */
int lucfg_get_bool(lucfg_handle_t* h, const char* sec, const char* key, int* out)
{
    pthread_mutex_lock(&h->lock);
    entry_t* e = find_entry(h, sec, key);
    int rc;
    if (!e)
    {
        rc = LUCFG_ERR_NOKEY;
        goto end;
    }
    /* 识别表达式 */
    if (e->has_expr)
    {
        *out = (e->expr_value != 0.0) ? 1 : 0;
        rc   = LUCFG_OK;
        goto end;
    }
    /* 识别整数 0/1 */
    if (e->type == 1)
    {
        if (e->ivalue == 0 || e->ivalue == 1)
        {
            *out = (int) e->ivalue;
            rc   = LUCFG_OK;
            goto end;
        }
        rc = LUCFG_ERR_RANGE;
        goto end;
    }
    /* 识别字符串 true/false (大小写不敏感) */
    if (e->type == 0)
    {
        if (str_icmp(e->value, "true") == 0 || str_icmp(e->value, "yes") == 0 ||
            str_icmp(e->value, "on") == 0)
        {
            *out = 1;
            rc   = LUCFG_OK;
            goto end;
        }
        if (str_icmp(e->value, "false") == 0 || str_icmp(e->value, "no") == 0 ||
            str_icmp(e->value, "off") == 0)
        {
            *out = 0;
            rc   = LUCFG_OK;
            goto end;
        }
    }
    rc = LUCFG_ERR_TYPE;
end:
    pthread_mutex_unlock(&h->lock);
    return rc;
}


// get all sections
int lucfg_get_sections(lucfg_handle_t* h, const char*** out_sections, int* out_count)
{
    pthread_mutex_lock(&h->lock);
    // 使用临时哈希表存储唯一的节名称
    typedef struct sec_entry
    {
        char* section;
        struct sec_entry* next;
    } sec_entry_t;
    sec_entry_t* sec_table[HASH_SIZE] = {0};
    int sec_count                     = 0;

    for (int i = 0; i < HASH_SIZE; ++i)
    {
        for (entry_t* e = h->table[i]; e; e = e->next)
        {
            unsigned idx = hash_func(e->section, ""); // 仅基于节名称哈希
            int found    = 0;
            for (sec_entry_t* se = sec_table[idx]; se; se = se->next)
            {
                if (str_icmp(se->section, e->section) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                sec_entry_t* new_se = malloc(sizeof(sec_entry_t));
                if (!new_se)
                    goto cleanup;
                new_se->section = strdup(e->section);
                if (!new_se->section)
                {
                    free(new_se);
                    goto cleanup;
                }
                new_se->next       = sec_table[idx];
                sec_table[idx]     = new_se;
                ++sec_count;
            }
        }
    }

    // 分配输出数组
    const char** sections = malloc(sizeof(char*) * sec_count);
    if (!sections)
        goto cleanup;

    int idx = 0;
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        for (sec_entry_t* se = sec_table[i]; se; se = se->next)
        {
            sections[idx++] = se->section; // 直接使用 strdup 的字符串
        }
    }

    *out_sections = sections;
    *out_count    = sec_count;

    // 清理临时哈希表，但不释放节名称字符串
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        sec_entry_t* se = sec_table[i];
        while (se)
        {
            sec_entry_t* tmp = se->next;
            free(se); // 仅释放结构体
            se = tmp;
        }
    }
    pthread_mutex_unlock(&h->lock);
    return LUCFG_OK;
cleanup:
    // 清理临时哈希表
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        sec_entry_t* se = sec_table[i];
        while (se)
        {
            sec_entry_t* tmp = se->next;
            free(se->section);
            free(se);
            se = tmp;
        }
    }
    return LUCFG_ERR_OPEN;
}
// get all keys in a section
int lucfg_get_keys(lucfg_handle_t* h, const char* section, const char*** out_keys, int* out_count)
{
    pthread_mutex_lock(&h->lock);
    // 使用临时哈希表存储唯一的键名称
    typedef struct key_entry
    {
        char* key;
        struct key_entry* next;
    } key_entry_t;
    key_entry_t* key_table[HASH_SIZE] = {0};
    int key_count                     = 0;

    for (int i = 0; i < HASH_SIZE; ++i)
    {
        for (entry_t* e = h->table[i]; e; e = e->next)
        {
            if (str_icmp(e->section, section) != 0)
                continue;
            unsigned idx = hash_func("", e->key); // 仅基于键名称哈希
            int found    = 0;
            for (key_entry_t* ke = key_table[idx]; ke; ke = ke->next)
            {
                if (str_icmp(ke->key, e->key) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                key_entry_t* new_ke = malloc(sizeof(key_entry_t));
                if (!new_ke)
                    goto cleanup;
                new_ke->key = strdup(e->key);
                if (!new_ke->key)
                {
                    free(new_ke);
                    goto cleanup;
                }
                new_ke->next      = key_table[idx];
                key_table[idx]    = new_ke;
                ++key_count;
            }
        }
    }

    // 分配输出数组
    const char** keys = malloc(sizeof(char*) * key_count);
    if (!keys)
        goto cleanup;

    int idx = 0;
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        for (key_entry_t* ke = key_table[i]; ke; ke = ke->next)
        {
            keys[idx++] = ke->key; // 直接使用 strdup 的字符串
        }
    }

    *out_keys  = keys;
    *out_count = key_count;

    // 清理临时哈希表，但不释放键名称字符串
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        key_entry_t* ke = key_table[i];
        while (ke)
        {
            key_entry_t* tmp = ke->next;
            free(ke); // 仅释放结构体
            ke = tmp;
        }
    }
    pthread_mutex_unlock(&h->lock);
    return LUCFG_OK;
cleanup:
    // 清理临时哈希表
    for (int i = 0; i < HASH_SIZE; ++i)
    {
        key_entry_t* ke = key_table[i];
        while (ke)
        {
            key_entry_t* tmp = ke->next;
            free(ke->key);
            free(ke);
            ke = tmp;
        }
    }
    return LUCFG_ERR_OPEN;
}
// free sections/keys array
void lucfg_free_array(const char** array, int count)
{
    if (!array)
        return;
    for (int i = 0; i < count; ++i)
        free((void*) array[i]);
    free(array);
}