#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#define PHASE_COUNT 6

enum place {
    NUZKY, 
    VRTACKA, 
    OHYBACKA, 
    SVARECKA, 
    LAKOVNA, 
    SROUBOVAK, 
    FREZA,
    _PLACE_COUNT
};

const char *place_str[_PLACE_COUNT] = {
    [NUZKY]     = "nuzky",
    [VRTACKA]   = "vrtacka",
    [OHYBACKA]  = "ohybacka",
    [SVARECKA]  = "svarecka",
    [LAKOVNA]   = "lakovna",
    [SROUBOVAK] = "sroubovak",
    [FREZA]     = "freza",
};

enum product {
    A, 
    B, 
    C,
    _PRODUCT_COUNT
};

const char *product_str[_PRODUCT_COUNT] = {
    [A] = "A",
    [B] = "B",
    [C] = "C",
};

const int operation_times_ms[_PLACE_COUNT] = {
    [NUZKY]     = 100,
    [VRTACKA]   = 200,
    [OHYBACKA]  = 150,
    [SVARECKA]  = 300,
    [LAKOVNA]   = 400,
    [SROUBOVAK] = 250,
    [FREZA]     = 500,
};

const enum place production_sequence[_PRODUCT_COUNT][PHASE_COUNT] = {
    [A] = {NUZKY, VRTACKA, OHYBACKA, SVARECKA, VRTACKA, LAKOVNA},
    [B] = {VRTACKA, NUZKY, FREZA, VRTACKA, LAKOVNA, SROUBOVAK},
    [C] = {FREZA, VRTACKA, SROUBOVAK, VRTACKA, FREZA, LAKOVNA},
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

int find_string_index(const char **array, int array_length, const char *search_str) {
    for (int i = 0; i < array_length; i++) {
        if (strcmp(array[i], search_str) == 0) {
            return i;
        }
    }
    return -1;
}

enum place get_workplace_for_phase(enum product prod, int phase) {
    if (prod < 0 || prod >= _PRODUCT_COUNT || phase < 0 || phase >= PHASE_COUNT) {
        fprintf(stderr, "ERROR: Invalid product (%d) or phase (%d)\n", prod, phase);
        exit(EXIT_FAILURE);
    }
    return production_sequence[prod][phase];
}

worker_t* find_worker_by_name(const char *name) {
    worker_t *current = factory.workers_list;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

worker_t* create_and_start_worker(const char *name, enum place specialization) {
    worker_t *new_worker = malloc(sizeof(worker_t));
    if (!new_worker) {
        fprintf(stderr, "ERROR: Failed to allocate memory for worker\n");
        exit(EXIT_FAILURE);
    }

    new_worker->name = strdup(name);
    if (!new_worker->name) {
        fprintf(stderr, "ERROR: Failed to allocate memory for worker name\n");
        free(new_worker);
        exit(EXIT_FAILURE);
    }
    
    new_worker->specialization = specialization;
    new_worker->should_terminate = false;
    new_worker->is_currently_working = false;
    new_worker->working_on_product = A;
    new_worker->working_on_phase = 0;
    new_worker->next = factory.workers_list;
    
    factory.workers_list = new_worker;

    if (pthread_create(&new_worker->thread, NULL, worker_main_loop, new_worker) != 0) {
        fprintf(stderr, "ERROR: Failed to create worker thread\n");
        factory.workers_list = new_worker->next;
        free(new_worker->name);
        free(new_worker);
        exit(EXIT_FAILURE);
    }
    
    return new_worker;
}

void deallocate_worker(worker_t *worker) {
    if (!worker) {
        return;
    }
    
    worker_t **current_ptr = &factory.workers_list;
    while (*current_ptr) {
        if (*current_ptr == worker) {
            *current_ptr = worker->next;
            break;
        }
        current_ptr = &(*current_ptr)->next;
    }
    
    free(worker->name);
    free(worker);
}

bool find_work_for_worker(worker_t *worker, enum product *out_prod, int *out_phase) {
    enum place worker_spec = worker->specialization;

    if (factory.available_workplaces[worker_spec] <= 0) {
        return false;
    }

    for (int phase = PHASE_COUNT - 1; phase >= 0; phase--) {
        for (int prod = 0; prod < _PRODUCT_COUNT; prod++) {
            if (factory.parts_in_phase[prod][phase] > 0) {
                if (production_sequence[prod][phase] == worker_spec) {
                    *out_prod = (enum product)prod;
                    *out_phase = phase;
                    return true;
                }
            }
        }
    }

    return false;
}

void complete_work(worker_t *worker, enum product prod, int phase) {
    enum place workplace = worker->specialization;

    if (phase == PHASE_COUNT - 1) {
        printf("done %s\n", product_str[prod]);
        fflush(stdout);
    } else {
        factory.parts_in_phase[prod][phase + 1]++;
    }

    if (factory.workplaces_pending_removal[workplace] > 0) {
        factory.workplaces_pending_removal[workplace]--;
        if (factory.total_workplaces[workplace] > 0) {
            factory.total_workplaces[workplace]--;
        }
    } else {
        factory.available_workplaces[workplace]++;
    }

    worker->is_currently_working = false;
    pthread_cond_broadcast(&factory.cond);
}

void execute_work(worker_t *worker, enum product prod, int phase) {
    enum place workplace = worker->specialization;

    if (factory.parts_in_phase[prod][phase] <= 0) {
        return;
    }
    
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

    complete_work(worker, prod, phase);
}

bool is_workplace_still_needed(enum place workplace_type) {
    /* Check waiting parts */
    for (int prod = 0; prod < _PRODUCT_COUNT; prod++) {
        for (int phase = 0; phase < PHASE_COUNT; phase++) {
            if (factory.parts_in_phase[prod][phase] > 0) {
                for (int future_phase = phase; future_phase < PHASE_COUNT; future_phase++) {
                    if (production_sequence[prod][future_phase] == workplace_type) {
                        return true;
                    }
                }
            }
        }
    }
    
    /* Check parts currently being worked on */
    worker_t *w = factory.workers_list;
    while (w) {
        if (w->is_currently_working) {
            enum product prod = w->working_on_product;
            int current_phase = w->working_on_phase;
            
            for (int future_phase = current_phase + 1; future_phase < PHASE_COUNT; future_phase++) {
                if (production_sequence[prod][future_phase] == workplace_type) {
                    return true;
                }
            }
        }
        w = w->next;
    }
    
    return false;
}

bool should_worker_stop(worker_t *worker) {
    if (worker->should_terminate && !worker->is_currently_working) {
        return true;
    }

    if (factory.is_shutting_down && !worker->is_currently_working) {
        if (!is_workplace_still_needed(worker->specialization)) {
            return true;
        }
    }

    return false;
}

void* worker_main_loop(void *arg) {
    worker_t *self = (worker_t*)arg;

    pthread_mutex_lock(&factory.mutex);

    while (true) {
        if (should_worker_stop(self)) {
            printf("%s goes home\n", self->name);
            fflush(stdout);
            pthread_mutex_unlock(&factory.mutex);
            return NULL;
        }

        enum product prod;
        int phase;
        if (find_work_for_worker(self, &prod, &phase)) {
            execute_work(self, prod, phase);
            continue;
        }

        pthread_cond_wait(&factory.cond, &factory.mutex);
    }

    pthread_mutex_unlock(&factory.mutex);
    return NULL;
}

void initialize_factory(void) {
    memset(&factory, 0, sizeof(factory_t));

    if (pthread_mutex_init(&factory.mutex, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize mutex\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&factory.cond, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize condition variable\n");
        pthread_mutex_destroy(&factory.mutex);
        exit(EXIT_FAILURE);
    }

    factory.workers_list = NULL;
    factory.is_shutting_down = false;
}

void destroy_factory(void) {
    pthread_mutex_destroy(&factory.mutex);
    pthread_cond_destroy(&factory.cond);

    worker_t *w = factory.workers_list;
    while (w) {
        worker_t *next = w->next;
        free(w->name);
        free(w);
        w = next;
    }
    factory.workers_list = NULL;
}

void add_workplace(enum place workplace) {
    factory.total_workplaces[workplace]++;
    factory.available_workplaces[workplace]++;
}

void schedule_workplace_removal(enum place workplace) {
    if (factory.available_workplaces[workplace] > 0) {
        factory.available_workplaces[workplace]--;
        if (factory.total_workplaces[workplace] > 0) {
            factory.total_workplaces[workplace]--;
        }
    } else {
        factory.workplaces_pending_removal[workplace]++;
    }
}

void request_worker_termination(const char *name) {
    worker_t *w = find_worker_by_name(name);
    if (w) {
        w->should_terminate = true;
    }
}

void wait_for_all_workers(void) {
    worker_t *worker = factory.workers_list;
    
    while (worker) {
        worker_t *next = worker->next;
        pthread_join(worker->thread, NULL);
        free(worker->name);
        free(worker);
        worker = next;
    }
    
    factory.workers_list = NULL;
}

int main(int argc, char **argv) {
    initialize_factory();
    
    char *line = NULL;
    size_t line_size = 0;
    
    while (true) {
        char *cmd, *arg1, *arg2, *arg3, *saveptr;

        if (getline(&line, &line_size, stdin) == -1) {
            break;
        }

        cmd = strtok_r(line, " \r\n", &saveptr);
        arg1 = strtok_r(NULL, " \r\n", &saveptr);
        arg2 = strtok_r(NULL, " \r\n", &saveptr);
        arg3 = strtok_r(NULL, " \r\n", &saveptr);

        if (!cmd) {
            continue;
        } else if (strcmp(cmd, "start") == 0 && arg1 && arg2 && !arg3) {
            int place = find_string_index(place_str, _PLACE_COUNT, arg2);
            if (place < 0) {
                fprintf(stderr, "Invalid workplace: %s\n", arg2);
                continue;
            }

            pthread_mutex_lock(&factory.mutex);
            create_and_start_worker(arg1, place);
            pthread_cond_broadcast(&factory.cond);
            pthread_mutex_unlock(&factory.mutex);
        }
        else if (strcmp(cmd, "make") == 0 && arg1 && !arg2) {
            int product = find_string_index(product_str, _PRODUCT_COUNT, arg1);
            if (product >= 0) {
                pthread_mutex_lock(&factory.mutex);
                factory.parts_in_phase[product][0]++;
                pthread_cond_broadcast(&factory.cond);
                pthread_mutex_unlock(&factory.mutex);
            } else {
                fprintf(stderr, "Invalid product: %s\n", arg1);
            }
        }
        else if (strcmp(cmd, "end") == 0 && arg1 && !arg2) {
            pthread_mutex_lock(&factory.mutex);
            request_worker_termination(arg1);
            pthread_cond_broadcast(&factory.cond);
            pthread_mutex_unlock(&factory.mutex);
        }
        else if (strcmp(cmd, "add") == 0 && arg1 && !arg2) {
            int place = find_string_index(place_str, _PLACE_COUNT, arg1);
            if (place >= 0) {
                pthread_mutex_lock(&factory.mutex);
                add_workplace(place);
                pthread_cond_broadcast(&factory.cond);
                pthread_mutex_unlock(&factory.mutex);
            } else {
                fprintf(stderr, "Invalid workplace: %s\n", arg1);
            }
        }
        else if (strcmp(cmd, "remove") == 0 && arg1 && !arg2) {
            int place = find_string_index(place_str, _PLACE_COUNT, arg1);
            if (place >= 0) {
                pthread_mutex_lock(&factory.mutex);
                schedule_workplace_removal(place);
                pthread_mutex_unlock(&factory.mutex);
            } else {
                fprintf(stderr, "Invalid workplace: %s\n", arg1);
            }
        }
        else {
            fprintf(stderr, "Invalid command: %s\n", line);
        }
    }
    
    cleanup_and_exit(line);
    return 0;
}

void cleanup_and_exit(char *line) {
    free(line);
    
    pthread_mutex_lock(&factory.mutex);
    factory.is_shutting_down = true;
    pthread_cond_broadcast(&factory.cond);
    pthread_mutex_unlock(&factory.mutex);
    
    wait_for_all_workers();
    destroy_factory();
}