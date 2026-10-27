#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "tatr.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#else
#include <unistd.h>
#endif

#include <dirent.h>

static int is_alnum_or_dash(int x) {
    return isalnum((unsigned char)x) || x == '-';
}

bool tatr_is_huid(const char *id) {
    if (!id) return false;
    for (int i = 0; i < 8; ++i) {
        if (!isdigit((unsigned char)*id++)) return false;
    }
    if (*id++ != '-') return false;
    for (int i = 0; i < 6; ++i) {
        if (!isdigit((unsigned char)*id++)) return false;
    }
    if (*id == '\0') return true;
    if (*id++ != '-') return false;
    if (*id == '\0') return true;
    while (*id) {
        if (!is_alnum_or_dash(*id++)) return false;
    }
    return true;
}

int tatr_status_is_closed(const TatrTask *t) {
    return t->status && strcmp(t->status, "CLOSED") == 0;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int set_error(char *error, size_t error_size, const char *message);
static int path_join(char *out, size_t out_size,
                     const char *dir, const char *name);

static char *dup_range(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static char *trim_dup(const char *s, size_t n) {
    while (n && isspace((unsigned char)*s)) {
        s++;
        n--;
    }
    while (n && isspace((unsigned char)s[n - 1])) n--;
    return dup_range(s, n);
}

static int is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static void list_grow(TatrList *list) {
    size_t ncap = list->cap ? list->cap * 2 : 32;
    TatrTask *ni = (TatrTask *)realloc(list->items, ncap * sizeof(TatrTask));
    if (!ni) return;
    list->items = ni;
    list->cap = ncap;
}

static void tags_grow(TatrTagDocs *docs) {
    size_t ncap = docs->cap ? docs->cap * 2 : 16;
    TatrTagDoc *ni = (TatrTagDoc *)realloc(docs->items, ncap * sizeof(TatrTagDoc));
    if (!ni) return;
    docs->items = ni;
    docs->cap = ncap;
}

static void free_task(TatrTask *t) {
    free(t->id);
    free(t->title);
    free(t->status);
    free(t->body);
    free(t->raw);
    free(t->path);
    for (int i = 0; i < t->ntags; ++i) free(t->tags[i]);
    for (int i = 0; i < t->nprops; ++i) {
        free(t->props[i].key);
        free(t->props[i].value);
    }
}

void tatr_list_free(TatrList *list) {
    for (size_t i = 0; i < list->count; ++i) free_task(&list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = list->cap = 0;
}

void tatr_tags_free(TatrTagDocs *docs) {
    for (size_t i = 0; i < docs->count; ++i) {
        free(docs->items[i].name);
        free(docs->items[i].desc);
    }
    free(docs->items);
    docs->items = NULL;
    docs->count = docs->cap = 0;
}

static void put_prop(TatrTask *t, const char *key, size_t kn, const char *val, size_t vn) {
    char *k = dup_range(key, kn);
    char *v = trim_dup(val, vn);
    if (!k || !v) {
        free(k);
        free(v);
        return;
    }
    for (int i = 0; i < t->nprops; ++i) {
        if (strcmp(t->props[i].key, k) == 0) {
            free(t->props[i].key);
            free(t->props[i].value);
            memmove(&t->props[i], &t->props[i + 1],
                    (size_t)(t->nprops - i - 1) * sizeof(TatrProp));
            t->nprops--;
            break;
        }
    }
    if (t->nprops < TATR_MAX_PROPS) {
        t->props[t->nprops].key = k;
        t->props[t->nprops].value = v;
        t->nprops++;
    } else {
        free(k);
        free(v);
    }
}

static const char *get_prop(const TatrTask *t, const char *key) {
    for (int i = 0; i < t->nprops; ++i) {
        if (strcmp(t->props[i].key, key) == 0) return t->props[i].value;
    }
    return NULL;
}

static void parse_tags(TatrTask *t, const char *s) {
    t->ntags = 0;
    if (!s) return;
    while (*s) {
        while (*s && (isspace((unsigned char)*s) || *s == ',')) s++;
        if (!*s) break;
        const char *start = s;
        while (*s && !isspace((unsigned char)*s) && *s != ',') s++;
        if (t->ntags < TATR_MAX_TAGS) {
            t->tags[t->ntags++] = dup_range(start, (size_t)(s - start));
        }
    }
}

static void parse_task_md(TatrTask *t, char *src) {
    char *p = src;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '#') {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        while (n && (p[n - 1] == '\r' || isspace((unsigned char)p[n - 1]))) n--;
        t->title = dup_range(p, n);
        p = eol ? eol + 1 : p + n;
    } else {
        t->title = xstrdup("!!! INVALID: TASK TITLE MUST START WITH # !!!");
        t->body = xstrdup(src);
        return;
    }

    for (;;) {
        char *line = p;
        char *eol = NULL;
        for (;;) {
            char *content = line;
            while (*content == ' ' || *content == '\t') content++;
            eol = strchr(line, '\n');
            char *line_end = eol ? eol : line + strlen(line);
            if (line_end > content && line_end[-1] == '\r') line_end--;
            if (content < line_end) break;
            if (!eol) {
                line = NULL;
                break;
            }
            line = eol + 1;
        }
        if (!line) break;

        char *key_start = line;
        while (*key_start == ' ' || *key_start == '\t') key_start++;
        if (*key_start != '-') break;
        char *key = key_start + 1;
        while (*key == ' ' || *key == '\t') key++;
        char *key_end = key;
        while (*key_end && isalnum((unsigned char)*key_end)) key_end++;
        char *colon = key_end;
        while (*colon == ' ' || *colon == '\t') colon++;
        if (key == key_end || *colon != ':') break;

        char *val = colon + 1;
        char *line_end = eol ? eol : line + strlen(line);
        size_t vn = (size_t)(line_end - val);
        if (vn && val[vn - 1] == '\r') vn--;
        put_prop(t, key, (size_t)(key_end - key), val, vn);
        p = eol ? eol + 1 : line_end;
    }
    t->body = xstrdup(p);

    const char *st = get_prop(t, "STATUS");
    t->status = xstrdup(st && *st ? st : "OPEN");
    const char *pr = get_prop(t, "PRIORITY");
    t->has_priority = pr && *pr;
    t->priority = pr && *pr ? atoi(pr) : TATR_UNSET_PRIORITY;
    parse_tags(t, get_prop(t, "TAGS"));
}

bool tatr_load_dir(TatrList *list, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        if (!tatr_is_huid(ent->d_name)) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (!is_dir(path)) continue;
        char mdpath[1080];
        snprintf(mdpath, sizeof(mdpath), "%s/TASK.md", path);
        char *raw = read_file(mdpath);
        if (!raw) continue;
        if (list->count >= list->cap) list_grow(list);
        if (list->count >= list->cap) {
            free(raw);
            break;
        }
        TatrTask t;
        memset(&t, 0, sizeof(t));
        t.id = xstrdup(ent->d_name);
        t.path = xstrdup(mdpath);
        t.raw = raw;
        parse_task_md(&t, raw);
        list->items[list->count++] = t;
    }
    closedir(d);
    return true;
}

bool tatr_load_tag_docs(TatrTagDocs *docs, const char *dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/tags", dir);
    char *raw = read_file(path);
    if (!raw) return true;
    char *p = raw;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        while (n && p[n - 1] == '\r') n--;
        char *line = p;
        p = eol ? eol + 1 : p + n;
        while (n && isspace((unsigned char)*line)) {
            line++;
            n--;
        }
        if (!n || *line == '#') continue;
        size_t i = 0;
        while (i < n && !isspace((unsigned char)line[i]) && line[i] != ',') i++;
        char *name = dup_range(line, i);
        while (i < n && (isspace((unsigned char)line[i]) || line[i] == ',')) i++;
        char *desc = trim_dup(line + i, n - i);
        if (docs->count >= docs->cap) tags_grow(docs);
        if (docs->count < docs->cap) {
            docs->items[docs->count].name = name;
            docs->items[docs->count].desc = desc;
            docs->count++;
        } else {
            free(name);
            free(desc);
        }
    }
    free(raw);
    return true;
}

bool tatr_create_tag(const char *tasks_dir, const char *name,
                     char *error, size_t error_size) {
    if (!tasks_dir || !*tasks_dir || !name || !*name || *name == '#') {
        return set_error(error, error_size, "Enter a valid tag name.");
    }
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (isspace(*p) || *p == ',')
            return set_error(error, error_size,
                             "Tag names cannot contain spaces or commas.");
    }

    TatrTagDocs docs = {0};
    if (!tatr_load_tag_docs(&docs, tasks_dir)) {
        tatr_tags_free(&docs);
        return set_error(error, error_size, "Unable to read project tags.");
    }
    for (size_t i = 0; i < docs.count; ++i) {
        if (strcmp(docs.items[i].name, name) == 0) {
            tatr_tags_free(&docs);
            return set_error(error, error_size, "That tag already exists.");
        }
    }
    tatr_tags_free(&docs);

    char path[1024];
    if (!path_join(path, sizeof(path), tasks_dir, "tags"))
        return set_error(error, error_size, "Tags file path is too long.");
    FILE *file = fopen(path, "a+b");
    if (!file) return set_error(error, error_size, "Unable to open the tags file.");

    int failed = 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        failed = 1;
    } else {
        long length = ftell(file);
        if (length < 0) {
            failed = 1;
        } else if (length > 0) {
            if (fseek(file, -1, SEEK_END) != 0) {
                failed = 1;
            } else {
                int last = fgetc(file);
                if (last == EOF ||
                    (last != '\n' &&
                     (fseek(file, 0, SEEK_END) != 0 ||
                      fputc('\n', file) == EOF)))
                    failed = 1;
            }
        }
    }
    if (!failed && fseek(file, 0, SEEK_END) != 0) failed = 1;
    if (!failed && fprintf(file, "%s\n", name) < 0) failed = 1;
    if (fflush(file) != 0 || ferror(file)) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed)
        return set_error(error, error_size, "Unable to write the tags file.");
    return true;
}

static int cmp_priority_desc(const void *a, const void *b) {
    const TatrTask *ta = (const TatrTask *)a;
    const TatrTask *tb = (const TatrTask *)b;
    if (ta->has_priority != tb->has_priority)
        return ta->has_priority ? -1 : 1;
    if (ta->priority > tb->priority) return -1;
    if (ta->priority < tb->priority) return 1;
    return strcmp(tb->id, ta->id);
}

static int cmp_priority_asc(const void *a, const void *b) {
    const TatrTask *ta = (const TatrTask *)a;
    const TatrTask *tb = (const TatrTask *)b;
    if (ta->has_priority != tb->has_priority)
        return ta->has_priority ? -1 : 1;
    if (ta->priority < tb->priority) return -1;
    if (ta->priority > tb->priority) return 1;
    return strcmp(ta->id, tb->id);
}

static int cmp_id_desc(const void *a, const void *b) {
    const TatrTask *ta = (const TatrTask *)a;
    const TatrTask *tb = (const TatrTask *)b;
    return strcmp(tb->id, ta->id);
}

static int cmp_id_asc(const void *a, const void *b) {
    const TatrTask *ta = (const TatrTask *)a;
    const TatrTask *tb = (const TatrTask *)b;
    return strcmp(ta->id, tb->id);
}

void tatr_sort(TatrList *list, bool by_id, bool ascending) {
    if (!list->items || list->count < 2) return;
    qsort(list->items, list->count, sizeof(TatrTask),
          by_id
              ? (ascending ? cmp_id_asc : cmp_id_desc)
              : (ascending ? cmp_priority_asc : cmp_priority_desc));
}

static int set_error(char *error, size_t error_size, const char *message) {
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

static int has_line_break(const char *value) {
    if (!value) return 0;
    return strchr(value, '\n') != NULL || strchr(value, '\r') != NULL;
}

static int path_join(char *out, size_t out_size,
                     const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    int separator = dir_len > 0 && dir[dir_len - 1] != '/' &&
                    dir[dir_len - 1] != '\\';
    int written = snprintf(out, out_size, "%s%s%s", dir,
                           separator ? "/" : "", name);
    return written >= 0 && (size_t)written < out_size;
}

static int write_task_file(const char *path, const TatrTask *task,
                           const char *title, const char *status,
                           bool has_priority, int priority,
                           const char *tags, const char *body,
                           char *error, size_t error_size) {
    if (!title || !*title || has_line_break(title) || has_line_break(tags) ||
        !status || (strcmp(status, "OPEN") != 0 &&
                    strcmp(status, "CLOSED") != 0))
        return set_error(error, error_size,
                         "Title, tags, or status contain invalid values.");

    char temp_path[2048];
    int written = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof(temp_path))
        return set_error(error, error_size, "Task file path is too long.");

    FILE *file = fopen(temp_path, "wb");
    if (!file) return set_error(error, error_size, "Unable to create task file.");

    int failed = fprintf(file, "# %s\n\n", title) < 0;
    for (int i = 0; !failed && task && i < task->nprops; ++i) {
        const char *key = task->props[i].key;
        if (strcmp(key, "STATUS") == 0 || strcmp(key, "PRIORITY") == 0 ||
            strcmp(key, "TAGS") == 0)
            continue;
        if (fprintf(file, "- %s: %s\n", key, task->props[i].value) < 0)
            failed = 1;
    }
    if (!failed && fprintf(file, "- STATUS: %s\n", status) < 0) failed = 1;
    if (!failed && has_priority &&
        fprintf(file, "- PRIORITY: %d\n", priority) < 0)
        failed = 1;
    if (!failed && tags && *tags && fprintf(file, "- TAGS: %s\n", tags) < 0)
        failed = 1;
    if (!failed && body && *body) {
        if (fputc('\n', file) == EOF || fputs(body, file) == EOF)
            failed = 1;
    }
    if (fflush(file) != 0 || ferror(file)) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        remove(temp_path);
        return set_error(error, error_size, "Unable to write the task file.");
    }

#ifdef _WIN32
    if (!MoveFileExA(temp_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(temp_path);
        return set_error(error, error_size, "Unable to replace the task file.");
    }
#else
    if (rename(temp_path, path) != 0) {
        remove(temp_path);
        return set_error(error, error_size, "Unable to replace the task file.");
    }
#endif
    return 1;
}

bool tatr_save_task(const char *tasks_dir, const TatrTask *task,
                    const char *title, const char *status,
                    bool has_priority, int priority,
                    const char *tags, const char *body,
                    char *error, size_t error_size) {
    if (!task || !task->path || !title || !*title || !status ||
        (strcmp(status, "OPEN") != 0 && strcmp(status, "CLOSED") != 0)) {
        return set_error(error, error_size, "Task title and status are required.");
    }
    (void)tasks_dir;
    return write_task_file(task->path, task, title, status, has_priority,
                           priority, tags, body, error, error_size) != 0;
}

bool tatr_create_task(const char *tasks_dir,
                     const char *title, const char *status,
                     bool has_priority, int priority,
                     const char *tags, const char *body,
                     char *created_id, size_t created_id_size,
                     char *error, size_t error_size) {
    if (!tasks_dir || !*tasks_dir || !title || !*title || !status ||
        (strcmp(status, "OPEN") != 0 && strcmp(status, "CLOSED") != 0) ||
        !created_id || !created_id_size) {
        return set_error(error, error_size, "Task title and status are required.");
    }

    time_t now = time(NULL);
    struct tm utc;
#ifdef _WIN32
    if (gmtime_s(&utc, &now) != 0)
        return set_error(error, error_size, "Unable to read the current UTC time.");
#else
    if (!gmtime_r(&now, &utc))
        return set_error(error, error_size, "Unable to read the current UTC time.");
#endif
    char base_id[32];
    if (strftime(base_id, sizeof(base_id), "%Y%m%d-%H%M%S", &utc) == 0)
        return set_error(error, error_size, "Unable to generate a task ID.");

    char task_dir[2048];
    char task_file[2048];
    for (unsigned suffix = 0; suffix < 1000; ++suffix) {
        if (suffix == 0) {
            if (snprintf(created_id, created_id_size, "%s", base_id) < 0 ||
                strlen(base_id) >= created_id_size)
                return set_error(error, error_size, "Task ID buffer is too small.");
        } else {
            int id_len = snprintf(created_id, created_id_size, "%s-%u",
                                  base_id, suffix);
            if (id_len < 0 || (size_t)id_len >= created_id_size)
                return set_error(error, error_size, "Task ID buffer is too small.");
        }
        if (!path_join(task_dir, sizeof(task_dir), tasks_dir, created_id))
            return set_error(error, error_size, "Tasks directory path is too long.");
        if (is_dir(task_dir)) continue;
#ifdef _WIN32
        if (_mkdir(task_dir) != 0) {
#else
        if (mkdir(task_dir, 0777) != 0) {
#endif
            if (errno == EEXIST) continue;
            return set_error(error, error_size, "Unable to create the task folder.");
        }
        if (!path_join(task_file, sizeof(task_file), task_dir, "TASK.md")) {
#ifdef _WIN32
            _rmdir(task_dir);
#else
            rmdir(task_dir);
#endif
            return set_error(error, error_size, "Task file path is too long.");
        }
        if (!write_task_file(task_file, NULL, title, status, has_priority,
                             priority, tags, body, error, error_size)) {
#ifdef _WIN32
            _rmdir(task_dir);
#else
            rmdir(task_dir);
#endif
            return false;
        }
        return true;
    }
    return set_error(error, error_size, "Unable to find an unused task ID.");
}

static int copy_path(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if (!dst_size || len >= dst_size) return 0;
    memcpy(dst, src, len + 1);
    return 1;
}

static int path_is_tasks_dir(const char *path) {
    size_t len = strlen(path);
    while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\')) --len;
    const char *base = path + len;
    while (base > path && base[-1] != '/' && base[-1] != '\\') --base;
    size_t base_len = (size_t)((path + len) - base);
    if (base_len != 5) return 0;
#ifdef _WIN32
    return _strnicmp(base, "tasks", base_len) == 0;
#else
    return strncmp(base, "tasks", base_len) == 0;
#endif
}

static int resolve_tasks_dir(const char *project_or_tasks_dir,
                             char *buf, size_t buflen) {
    if (!project_or_tasks_dir || !*project_or_tasks_dir) return 0;
    if (path_is_tasks_dir(project_or_tasks_dir) &&
        is_dir(project_or_tasks_dir)) {
        return copy_path(buf, buflen, project_or_tasks_dir);
    }

    size_t len = strlen(project_or_tasks_dir);
    int separator = len > 0 && (project_or_tasks_dir[len - 1] == '/' ||
                                project_or_tasks_dir[len - 1] == '\\');
    int written = snprintf(buf, buflen, "%s%stasks", project_or_tasks_dir,
                           separator ? "" : "/");
    return written >= 0 && (size_t)written < buflen && is_dir(buf);
}

int tatr_get_current_dir(char *buf, size_t buflen) {
#ifdef _WIN32
    if (buflen > INT_MAX || !_getcwd(buf, (int)buflen)) return 0;
#else
    if (!getcwd(buf, buflen)) return 0;
#endif
    return 1;
}

const char *tatr_find_tasks_dir(const char *explicit_dir, char *buf, size_t buflen) {
    if (!buf || !buflen) return NULL;
    buf[0] = 0;
    if (explicit_dir) {
        return resolve_tasks_dir(explicit_dir, buf, buflen) ? buf : NULL;
    }

    char cur[4096];
    if (!tatr_get_current_dir(cur, sizeof(cur))) return NULL;
    for (;;) {
        if (resolve_tasks_dir(cur, buf, buflen)) return buf;
        size_t len = strlen(cur);
        while (len > 1 && (cur[len - 1] == '/' || cur[len - 1] == '\\')) {
            cur[--len] = 0;
        }
        char *slash = strrchr(cur, '/');
#ifdef _WIN32
        char *bslash = strrchr(cur, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
        if (!slash) break;
#ifdef _WIN32
        if (slash == cur + 2 && cur[1] == ':') {
            slash[1] = 0;
        } else
#endif
        if (slash == cur) {
            slash[1] = 0;
            if (resolve_tasks_dir(cur, buf, buflen)) return buf;
            break;
        } else {
            *slash = 0;
        }
    }
    return NULL;
}

void tatr_format_tags(const TatrTask *t, char *out, size_t n) {
    out[0] = 0;
    size_t used = 0;
    for (int i = 0; i < t->ntags; ++i) {
        int w = snprintf(out + used, n > used ? n - used : 0, "%s%s",
                         i ? "," : "", t->tags[i]);
        if (w < 0) break;
        used += (size_t)w;
        if (used >= n) break;
    }
}

const char *tatr_tag_desc(const TatrTagDocs *docs, const char *name) {
    if (!docs || !name) return NULL;
    for (size_t i = 0; i < docs->count; ++i) {
        if (strcmp(docs->items[i].name, name) == 0) return docs->items[i].desc;
    }
    return NULL;
}
