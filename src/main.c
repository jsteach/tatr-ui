#define FENSTER_IMPL
#include "vendor/fenster.h"

#include "tatr.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s [project-or-tasks-directory]\n", program);
    fprintf(stderr, "Reads TASK.md files from TATR task folders and opens a desktop browser.\n");
}

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))) {
        print_usage(argv[0]);
        return argc == 2 ? 0 : 2;
    }

    char tasks_dir[4096] = {0};
    if (argc == 2 &&
        !tatr_find_tasks_dir(argv[1], tasks_dir, sizeof(tasks_dir))) {
        fprintf(stderr, "No TATR tasks directory found at: %s\n", argv[1]);
        return 1;
    }

    TatrList tasks = {0};
    TatrTagDocs tags = {0};
    if (tasks_dir[0]) {
        if (!tatr_load_dir(&tasks, tasks_dir)) {
            fprintf(stderr, "Unable to read TATR tasks directory: %s\n", tasks_dir);
            return 1;
        }
        if (!tatr_load_tag_docs(&tags, tasks_dir)) {
            fprintf(stderr, "Unable to read tag documentation in: %s\n", tasks_dir);
            tatr_list_free(&tasks);
            return 1;
        }
    }

    tatr_sort(&tasks, false, false);
    int result = tatr_desktop_ui(&tasks, &tags, tasks_dir, sizeof(tasks_dir));
    tatr_tags_free(&tags);
    tatr_list_free(&tasks);
    return result;
}
