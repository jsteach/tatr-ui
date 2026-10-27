#include "tatr.h"

#include <assert.h>
#include <string.h>

static void check_priority_order(bool ascending, const char *first,
                                 const char *second, const char *third) {
    TatrTask tasks[] = {
        {.id = "unset-a", .priority = TATR_UNSET_PRIORITY},
        {.id = "low", .priority = 2, .has_priority = true},
        {.id = "high", .priority = 1000, .has_priority = true},
        {.id = "unset-b", .priority = TATR_UNSET_PRIORITY}
    };
    TatrList list = {tasks, sizeof(tasks) / sizeof(tasks[0]),
                     sizeof(tasks) / sizeof(tasks[0])};

    tatr_sort(&list, false, ascending);
    assert(strcmp(list.items[0].id, first) == 0);
    assert(strcmp(list.items[1].id, second) == 0);
    assert(strcmp(list.items[2].id, third) == 0);
    assert(!list.items[3].has_priority);
}

int main(void) {
    check_priority_order(false, "high", "low", "unset-b");
    check_priority_order(true, "low", "high", "unset-a");

    TatrList parsed = {0};
    assert(tatr_load_dir(&parsed, "tests/fixtures/priority"));
    assert(parsed.count == 2);
    int found_numeric_priority = 0;
    int found_unset_priority = 0;
    for (size_t i = 0; i < parsed.count; ++i) {
        if (parsed.items[i].has_priority) {
            assert(parsed.items[i].priority == 1000);
            assert(strcmp(parsed.items[i].title,
                          "Priority is a parsed numeric property") == 0);
            found_numeric_priority = 1;
        } else {
            found_unset_priority = 1;
        }
    }
    assert(found_numeric_priority);
    assert(found_unset_priority);
    tatr_sort(&parsed, false, false);
    assert(parsed.items[0].has_priority);
    assert(parsed.items[0].priority == 1000);
    assert(!parsed.items[1].has_priority);
    tatr_list_free(&parsed);

    char tasks_dir[1024];
    assert(tatr_find_tasks_dir("tests/fixtures/project", tasks_dir,
                               sizeof(tasks_dir)) != NULL);
    assert(strcmp(tasks_dir, "tests/fixtures/project/tasks") == 0);
    assert(tatr_find_tasks_dir("tests/fixtures/project/tasks", tasks_dir,
                               sizeof(tasks_dir)) != NULL);
    assert(strcmp(tasks_dir, "tests/fixtures/project/tasks") == 0);
    assert(tatr_find_tasks_dir("tests/fixtures", tasks_dir,
                               sizeof(tasks_dir)) == NULL);
    return 0;
}
