#ifndef _CERVUS_JSON_H
#define _CERVUS_JSON_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json json_t;

typedef struct {
    int  line;
    int  column;
    char message[96];
} json_error_t;

json_t     *json_parse(const char *text, json_error_t *err);
json_t     *json_parse_len(const char *text, size_t len, json_error_t *err);
json_t     *json_parse_file(const char *path, json_error_t *err);
void        json_free(json_t *v);

json_type_t json_type(const json_t *v);
int         json_is_null(const json_t *v);

const char *json_string(const json_t *v, const char *fallback);
double      json_number(const json_t *v, double fallback);
long        json_int(const json_t *v, long fallback);
int         json_bool(const json_t *v, int fallback);

size_t      json_len(const json_t *v);
json_t     *json_at(const json_t *arr, size_t index);
json_t     *json_get(const json_t *obj, const char *key);
const char *json_key(const json_t *obj, size_t index);
json_t     *json_value_at(const json_t *obj, size_t index);
json_t     *json_query(const json_t *root, const char *path);

json_t     *json_new_null(void);
json_t     *json_new_bool(int b);
json_t     *json_new_number(double n);
json_t     *json_new_string(const char *s);
json_t     *json_new_array(void);
json_t     *json_new_object(void);

int         json_append(json_t *arr, json_t *item);
int         json_set(json_t *obj, const char *key, json_t *value);
int         json_remove(json_t *obj, const char *key);

char       *json_dump(const json_t *v, int indent);
int         json_dump_file(const json_t *v, const char *path, int indent);

#endif
