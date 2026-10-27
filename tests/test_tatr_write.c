#include "tatr.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define get_process_id _getpid
#define make_directory(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define get_process_id getpid
#define make_directory(path) mkdir(path, 0777)
#endif

int main(void) {
    char root[256];
    char tasks_dir[320];
    char task_dir[384];
    char task_file[448];
    int root_len = snprintf(root, sizeof(root), "build/test-tatr-write-%ld-%ld",
                            (long)time(NULL), (long)get_process_id());
    assert(root_len > 0 && (size_t)root_len < sizeof(root));
    assert(make_directory(root) == 0);

    int tasks_len = snprintf(tasks_dir, sizeof(tasks_dir), "%s/tasks", root);
    assert(tasks_len > 0 && (size_t)tasks_len < sizeof(tasks_dir));
    assert(make_directory(tasks_dir) == 0);

    char error[160] = {0};
    assert(tatr_create_tag(tasks_dir, "existing-tag", error, sizeof(error)));
    TatrTagDocs tag_docs = {0};
    assert(tatr_load_tag_docs(&tag_docs, tasks_dir));
    assert(tag_docs.count == 1);
    assert(strcmp(tag_docs.items[0].name, "existing-tag") == 0);
    tatr_tags_free(&tag_docs);
    assert(!tatr_create_tag(tasks_dir, "existing-tag", error, sizeof(error)));
    assert(!tatr_create_tag(tasks_dir, "bad tag", error, sizeof(error)));
    char tags_path[384];
    int tags_path_len = snprintf(tags_path, sizeof(tags_path), "%s/tags",
                                 tasks_dir);
    assert(tags_path_len > 0 && (size_t)tags_path_len < sizeof(tags_path));
    FILE *tags_file = fopen(tags_path, "wb");
    assert(tags_file != NULL);
    assert(fputs("existing-tag", tags_file) >= 0);
    assert(fclose(tags_file) == 0);
    assert(tatr_create_tag(tasks_dir, "second-tag", error, sizeof(error)));
    assert(tatr_load_tag_docs(&tag_docs, tasks_dir));
    assert(tag_docs.count == 2);
    assert(strcmp(tag_docs.items[1].name, "second-tag") == 0);
    tatr_tags_free(&tag_docs);

    int task_dir_len = snprintf(task_dir, sizeof(task_dir),
                                "%s/20261027-123456", tasks_dir);
    assert(task_dir_len > 0 && (size_t)task_dir_len < sizeof(task_dir));
    assert(make_directory(task_dir) == 0);
    int file_len = snprintf(task_file, sizeof(task_file), "%s/TASK.md", task_dir);
    assert(file_len > 0 && (size_t)file_len < sizeof(task_file));
    FILE *file = fopen(task_file, "wb");
    assert(file != NULL);
    assert(fputs("# Original title\n\n- STATUS: OPEN\n- PRIORITY: 4\n"
                 "- CUSTOM: keep-me\n- TAGS: old-tag\n\nOriginal body\n",
                 file) >= 0);
    assert(fclose(file) == 0);

    TatrList tasks = {0};
    assert(tatr_load_dir(&tasks, tasks_dir));
    assert(tasks.count == 1);
    assert(tatr_save_task(tasks_dir, &tasks.items[0], "Updated title", "CLOSED",
                          true, 1000, "new-tag, second-tag", "Updated body",
                          error, sizeof(error)));

    TatrList reloaded = {0};
    assert(tatr_load_dir(&reloaded, tasks_dir));
    assert(reloaded.count == 1);
    TatrTask *updated = &reloaded.items[0];
    assert(strcmp(updated->title, "Updated title") == 0);
    assert(strcmp(updated->status, "CLOSED") == 0);
    assert(updated->has_priority && updated->priority == 1000);
    assert(updated->ntags == 2);
    assert(strcmp(updated->tags[0], "new-tag") == 0);
    assert(strcmp(updated->tags[1], "second-tag") == 0);
    assert(strstr(updated->body, "Updated body") != NULL);
    int preserved_custom_property = 0;
    for (int i = 0; i < updated->nprops; ++i) {
        if (strcmp(updated->props[i].key, "CUSTOM") == 0 &&
            strcmp(updated->props[i].value, "keep-me") == 0)
            preserved_custom_property = 1;
    }
    assert(preserved_custom_property);
    tatr_list_free(&tasks);
    tatr_list_free(&reloaded);

    char created_id[64];
    assert(tatr_create_task(tasks_dir, "Created title", "OPEN", false, 0,
                            "created-tag", "Created body", created_id,
                            sizeof(created_id), error, sizeof(error)));
    assert(tatr_is_huid(created_id));

    TatrList with_created_task = {0};
    assert(tatr_load_dir(&with_created_task, tasks_dir));
    assert(with_created_task.count == 2);
    int found_created_task = 0;
    for (size_t i = 0; i < with_created_task.count; ++i) {
        TatrTask *task = &with_created_task.items[i];
        if (strcmp(task->id, created_id) != 0) continue;
        assert(strcmp(task->title, "Created title") == 0);
        assert(strcmp(task->status, "OPEN") == 0);
        assert(!task->has_priority);
        assert(task->ntags == 1);
        assert(strcmp(task->tags[0], "created-tag") == 0);
        assert(strstr(task->body, "Created body") != NULL);
        found_created_task = 1;
    }
    assert(found_created_task);
    tatr_list_free(&with_created_task);
    return 0;
}
