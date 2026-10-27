#define FENSTER_IMPL
#include "vendor/fenster.h"

#include "tatr.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s [tasks-directory]\n", program);
    fprintf(stderr, "Reads TASK.md files from TATR task folders and opens a desktop browser.\n");
}

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))) {
        print_usage(argv[0]);
        return argc == 2 ? 0 : 2;
    }

    char tasks_dir[1024];
    const char *found = tatr_find_tasks_dir(argc == 2 ? argv[1] : NULL,
                                             tasks_dir, sizeof(tasks_dir));
    if (!found) {
        fprintf(stderr, "Unable to determine the tasks directory.\n");
        return 1;
    }

    TatrList tasks = {0};
    TatrTagDocs tags = {0};
    if (!tatr_load_dir(&tasks, found)) {
        fprintf(stderr, "Unable to read TATR tasks directory: %s\n", found);
        return 1;
    }
    if (!tatr_load_tag_docs(&tags, found)) {
        fprintf(stderr, "Unable to read tag documentation in: %s\n", found);
        tatr_list_free(&tasks);
        return 1;
    }

    tatr_sort(&tasks, false, false);
    int result = tatr_desktop_ui(&tasks, &tags, found);
    tatr_tags_free(&tags);
    tatr_list_free(&tasks);
    return result;
}
