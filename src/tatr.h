#pragma once

#include <stddef.h>
#include <stdbool.h>

#define TATR_UNSET_PRIORITY 999999
#define TATR_DEFAULT_PRIORITY 100
#define TATR_MAX_TAGS 32
#define TATR_MAX_PROPS 32

typedef struct {
    char *key;
    char *value;
} TatrProp;

typedef struct {
    char *id;
    char *title;
    char *status;
    int priority;
    bool has_priority;
    char *tags[TATR_MAX_TAGS];
    int ntags;
    TatrProp props[TATR_MAX_PROPS];
    int nprops;
    char *body;
    char *raw;
    char *path;
} TatrTask;

typedef struct {
    TatrTask *items;
    size_t count;
    size_t cap;
} TatrList;

typedef struct {
    char *name;
    char *desc;
} TatrTagDoc;

typedef struct {
    TatrTagDoc *items;
    size_t count;
    size_t cap;
} TatrTagDocs;

bool tatr_is_huid(const char *id);
void tatr_list_free(TatrList *list);
void tatr_tags_free(TatrTagDocs *docs);
bool tatr_load_dir(TatrList *list, const char *dir);
bool tatr_load_tag_docs(TatrTagDocs *docs, const char *dir);
void tatr_sort(TatrList *list, bool by_id, bool ascending);
const char *tatr_find_tasks_dir(const char *explicit_dir, char *buf, size_t buflen);
int tatr_get_current_dir(char *buf, size_t buflen);
int tatr_status_is_closed(const TatrTask *t);
void tatr_format_tags(const TatrTask *t, char *out, size_t n);
const char *tatr_tag_desc(const TatrTagDocs *docs, const char *name);
