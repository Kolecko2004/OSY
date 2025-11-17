#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#define PHASE_COUNT 6

enum place {
    NUZKY, VRTACKA, OHYBACKA, SVARECKA, LAKOVNA, SROUBOVAK, FREZA, _PLACE_COUNT
};

const char *place_str[_PLACE_COUNT] = {
    [NUZKY] = "nuzky", [VRTACKA] = "vrtacka", [OHYBACKA] = "ohybacka",
    [SVARECKA] = "svarecka", [LAKOVNA] = "lakovna", [SROUBOVAK] = "sroubovak", [FREZA] = "freza"
};

enum product { A, B, C, _PRODUCT_COUNT };

const char *product_str[_PRODUCT_COUNT] = { [A] = "A", [B] = "B", [C] = "C" };

const int operation_times_ms[_PLACE_COUNT] = {
    [NUZKY] = 100, [VRTACKA] = 200, [OHYBACKA] = 150,
    [SVARECKA] = 300, [LAKOVNA] = 400, [SROUBOVAK] = 250, [FREZA] = 500
};

const enum place production_sequence[_PRODUCT_COUNT][PHASE_COUNT] = {
    [A] = {NUZKY, VRTACKA, OHYBACKA, SVARECKA, VRTACKA, LAKOVNA},
    [B] = {VRTACKA, NUZKY, FREZA, VRTACKA, LAKOVNA, SROUBOVAK},
    [C] = {FREZA, VRTACKA, SROUBOVAK, VRTACKA, FREZA, LAKOVNA}
};

typedef struct worker {
    char *name;
    enum place specialization;
    pthread_t thread;
    bool should_terminate;      
    bool is_currently_working;
    enum product working_on_product;
    int working_on_phase;
    struct worker *next;
} worker_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int available_workplaces[_PLACE_COUNT];
    int total_workplaces[_PLACE_COUNT];
    int workplaces_pending_removal[_PLACE_COUNT];
    int parts_in_phase[_PRODUCT_COUNT][PHASE_COUNT];
    worker_t *workers_list;
    bool is_shutting_down;        
} factory_t;

factory_t factory;

void* worker_main_loop(void *arg);
void cleanup_and_exit(char *line);
static int count_workers_for_place(enum place p, worker_t *include_self);
static int count_working_on_place(enum place p);
static bool can_part_reach_place(enum product prod, int start_phase, enum place target, worker_t *self);
static bool is_workplace_needed(worker_t *self, enum place workplace);

int find_string_index(const char **array, int array_length, const char *search_str) {
    for (int i = 0; i < array_length; i++) {
        if (strcmp(array[i], search_str) == 0) return i;
    }
    return -1;
}

worker_t* find_worker_by_name(const char *name) {
    for (worker_t *w = factory.workers_list; w; w = w->next) {
        if (strcmp(w->name, name) == 0) return w;
    }
    return NULL;
}

static int count_workers_for_place(enum place p, worker_t *include_self) {
    int cnt = 0;
    for (worker_t *w = factory.workers_list; w; w = w->next) {
        if (w->specialization == p) {
            if (!w->should_terminate || (include_self && w == include_self)) cnt++;
        }
    }
    return cnt;
}

static int count_working_on_place(enum place p) {
    int cnt = 0;
    for (worker_t *w = factory.workers_list; w; w = w->next) {
        if (w->specialization == p && w->is_currently_working) cnt++;
    }
    return cnt;
}

static bool can_part_reach_place(enum product prod, int start_phase, enum place target, worker_t *self) {
    for (int future = start_phase; future < PHASE_COUNT; future++) {
        if (production_sequence[prod][future] != target) continue;

        bool path_ok = true;
        for (int k = start_phase; k <= future; k++) {
            enum place p = production_sequence[prod][k];

            if (factory.total_workplaces[p] <= 0) { path_ok = false; break; }

            int workers_available = count_workers_for_place(p, self);
            int working_now = count_working_on_place(p);

            if (workers_available + working_now <= 0) { path_ok = false; break; }
        }

        if (path_ok) return true;
    }
    return false;
}

static bool is_workplace_needed(worker_t *self, enum place workplace) {
    for (int prod = 0; prod < _PRODUCT_COUNT; prod++) {
        for (int phase = 0; phase < PHASE_COUNT; phase++) {
            if (factory.parts_in_phase[prod][phase] > 0) {
                if (can_part_reach_place((enum product)prod, phase, workplace, self)) return true;
            }
        }
    }

    for (worker_t *w = factory.workers_list; w; w = w->next) {
        if (w->is_currently_working) {
            int next_phase = w->working_on_phase + 1;
            if (next_phase < PHASE_COUNT) {
                if (can_part_reach_place(w->working_on_product, next_phase, workplace, self)) return true;
            }
        }
    }

    return false;
}

bool find_work_for_worker(worker_t *worker, enum product *out_prod, int *out_phase) {
    enum place spec = worker->specialization;
    if (factory.available_workplaces[spec] <= 0) return false;

    for (int phase = PHASE_COUNT - 1; phase >= 0; phase--) {
        for (int prod = 0; prod < _PRODUCT_COUNT; prod++) {
            if (factory.parts_in_phase[prod][phase] > 0 && production_sequence[prod][phase] == spec) {
                *out_prod = (enum product)prod;
                *out_phase = phase;
                return true;
            }
        }
    }
    return false;
}

void execute_work(worker_t *worker, enum product prod, int phase) {
    enum place workplace = worker->specialization;
    if (factory.parts_in_phase[prod][phase] <= 0) return;

    factory.parts_in_phase[prod][phase]--;
    factory.available_workplaces[workplace]--;

    worker->is_currently_working = true;
    worker->working_on_product = prod;
    worker->working_on_phase = phase;

    printf("%s %s %d %s\n", worker->name, place_str[workplace], phase + 1, product_str[prod]);
    fflush(stdout);

    pthread_mutex_unlock(&factory.mutex);
    usleep(operation_times_ms[workplace] * 1000);
    pthread_mutex_lock(&factory.mutex);

    if (phase == PHASE_COUNT - 1) {
        printf("done %s\n", product_str[prod]);
        fflush(stdout);
    } else {
        factory.parts_in_phase[prod][phase + 1]++;
    }

    if (factory.workplaces_pending_removal[workplace] > 0) {
        factory.workplaces_pending_removal[workplace]--;
        if (factory.total_workplaces[workplace] > 0) factory.total_workplaces[workplace]--;
    } else {
        factory.available_workplaces[workplace]++;
    }

    worker->is_currently_working = false;
    pthread_cond_broadcast(&factory.cond);
}

void* worker_main_loop(void *arg) {
    worker_t *self = (worker_t*)arg;
    pthread_mutex_lock(&factory.mutex);

    for (;;) {
        enum product prod;
        int phase;

        if (self->should_terminate && !self->is_currently_working && !find_work_for_worker(self, &prod, &phase)) {
            printf("%s goes home\n", self->name);
            fflush(stdout);
            pthread_mutex_unlock(&factory.mutex);
            return NULL;
        }

        if (factory.is_shutting_down && !self->is_currently_working && !find_work_for_worker(self, &prod, &phase)) {
            if (!is_workplace_needed(self, self->specialization)) {
                printf("%s goes home\n", self->name);
                fprintf(stderr, "%s goes home here\n", self->name);
                fflush(stdout);
                pthread_mutex_unlock(&factory.mutex);
                return NULL;
            }

        }

        if (!self->should_terminate && find_work_for_worker(self, &prod, &phase)) {
            execute_work(self, prod, phase);
            continue;
        }

        pthread_cond_wait(&factory.cond, &factory.mutex);
    }
}

worker_t* create_worker_locked(const char *name, enum place spec) {
    worker_t *w = malloc(sizeof(worker_t));
    if (!w) { perror("malloc"); exit(EXIT_FAILURE); }
    w->name = strdup(name);
    if (!w->name) { perror("strdup"); exit(EXIT_FAILURE); }
    w->specialization = spec;
    w->should_terminate = false;
    w->is_currently_working = false;
    w->working_on_product = A;
    w->working_on_phase = 0;
    w->next = factory.workers_list;
    factory.workers_list = w;

    if (pthread_create(&w->thread, NULL, worker_main_loop, w) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    return w;
}

void initialize_factory(void) {
    memset(&factory, 0, sizeof(factory_t));
    pthread_mutex_init(&factory.mutex, NULL);
    pthread_cond_init(&factory.cond, NULL);
    factory.workers_list = NULL;
    factory.is_shutting_down = false;
}

void wait_and_destroy_workers(void) {
    for (worker_t *w = factory.workers_list; w; w = w->next) {
        pthread_join(w->thread, NULL);
    }

    worker_t *w = factory.workers_list;
    while (w) {
        worker_t *next = w->next;
        free(w->name);
        free(w);
        w = next;
    }
    factory.workers_list = NULL;
}

void cleanup_and_exit(char *line) {
    free(line);

    pthread_mutex_lock(&factory.mutex);
    factory.is_shutting_down = true;
    pthread_cond_broadcast(&factory.cond);
    pthread_mutex_unlock(&factory.mutex);

    wait_and_destroy_workers();

    pthread_mutex_destroy(&factory.mutex);
    pthread_cond_destroy(&factory.cond);
}

int main() {
    initialize_factory();

    char *line = NULL;
    size_t sz = 0;

    while (true) {
        char *cmd, *arg1, *arg2, *arg3, *saveptr;
        ssize_t r = getline(&line, &sz, stdin);
        if (r == -1) break;

        cmd = strtok_r(line, " \r\n", &saveptr);
        arg1 = strtok_r(NULL, " \r\n", &saveptr);
        arg2 = strtok_r(NULL, " \r\n", &saveptr);
        arg3 = strtok_r(NULL, " \r\n", &saveptr);

        if (!cmd) continue;

        pthread_mutex_lock(&factory.mutex);

        if (strcmp(cmd, "start") == 0 && arg1 && arg2 && !arg3) {
            int place = find_string_index(place_str, _PLACE_COUNT, arg2);
            if (place < 0) {
                fprintf(stderr, "Invalid workplace: %s\n", arg2);
            } else {
                create_worker_locked(arg1, place);
                pthread_cond_broadcast(&factory.cond);
            }
        }
        else if (strcmp(cmd, "make") == 0 && arg1 && !arg2) {
            int prod = find_string_index(product_str, _PRODUCT_COUNT, arg1);
            if (prod < 0) {
                fprintf(stderr, "Invalid product: %s\n", arg1);
            } else {
                factory.parts_in_phase[prod][0]++;
                pthread_cond_broadcast(&factory.cond);
            }
        }
        else if (strcmp(cmd, "end") == 0 && arg1 && !arg2) {
            worker_t *w = find_worker_by_name(arg1);
            if (!w) {
                fprintf(stderr, "Invalid worker: %s\n", arg1);
            } else {
                w->should_terminate = true;
                pthread_cond_broadcast(&factory.cond);
            }
        }
        else if (strcmp(cmd, "add") == 0 && arg1 && !arg2) {
            int place = find_string_index(place_str, _PLACE_COUNT, arg1);
            if (place < 0) {
                fprintf(stderr, "Invalid workplace: %s\n", arg1);
            } else {
                factory.total_workplaces[place]++;
                factory.available_workplaces[place]++;
                pthread_cond_broadcast(&factory.cond);
            }
        }
        else if (strcmp(cmd, "remove") == 0 && arg1 && !arg2) {
            int place = find_string_index(place_str, _PLACE_COUNT, arg1);
            if (place < 0) {
                fprintf(stderr, "Invalid workplace: %s\n", arg1);
            } else {
                if (factory.available_workplaces[place] > 0) {
                    factory.available_workplaces[place]--;
                    if (factory.total_workplaces[place] > 0) factory.total_workplaces[place]--;
                } else {
                    factory.workplaces_pending_removal[place]++;
                }
            }
        }
        else {
            fprintf(stderr, "Invalid command: %s\n", line);
        }

        pthread_mutex_unlock(&factory.mutex);
    }

    cleanup_and_exit(line);
    return 0;
}
