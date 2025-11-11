#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

typedef struct item {
    int x;
    char *text;
    struct item *next;
} item_t;

typedef struct {
    item_t *head;
    item_t *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_t output_mutex;
    int producer_done;
    int invalid_input;
} queue_t;

typedef struct {
    int thread_id;
    queue_t *queue;
} consumer_args_t;

queue_t queue = {
    .head = NULL,
    .tail = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .output_mutex = PTHREAD_MUTEX_INITIALIZER,
    .producer_done = 0,
    .invalid_input = 0
};

void enqueue(queue_t *q, int x, char *text) {
    item_t *new_item = (item_t *)malloc(sizeof(item_t));
    new_item->x = x;
    new_item->text = text;
    new_item->next = NULL;

    pthread_mutex_lock(&q->mutex);
    
    if (q->tail == NULL) {
        q->head = new_item;
        q->tail = new_item;
    } else {
        q->tail->next = new_item;
        q->tail = new_item;
    }
    
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

item_t *dequeue(queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    
    while (q->head == NULL && !q->producer_done) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    
    item_t *item = q->head;
    if (item != NULL) {
        q->head = item->next;
        if (q->head == NULL) {
            q->tail = NULL;
        }
    }
    
    pthread_mutex_unlock(&q->mutex);
    return item;
}

void *producer_thread(void *arg) {
    queue_t *q = (queue_t *)arg;
    int ret, x;
    char *text;
    
    while ((ret = scanf("%d %ms", &x, &text)) == 2) {
        if (x < 0) {
            free(text);
            pthread_mutex_lock(&q->mutex);
            q->invalid_input = 1;
            q->producer_done = 1;
            pthread_cond_broadcast(&q->cond);
            pthread_mutex_unlock(&q->mutex);
            return NULL;
        }
        enqueue(q, x, text);
    }
    
    if (ret != EOF && ret != -1) {
        pthread_mutex_lock(&q->mutex);
        q->invalid_input = 1;
        q->producer_done = 1;
        pthread_cond_broadcast(&q->cond);
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    
    pthread_mutex_lock(&q->mutex);
    q->producer_done = 1;
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    
    return NULL;
}

void *consumer_thread(void *arg) {
    consumer_args_t *args = (consumer_args_t *)arg;
    int thread_id = args->thread_id;
    queue_t *q = args->queue;
    
    while (1) {
        item_t *item = dequeue(q);
        
        if (item == NULL) {
            break;
        }
        
        pthread_mutex_lock(&q->output_mutex);
        printf("Thread %d:", thread_id);
        for (int i = 0; i < item->x; i++) {
            printf(" %s", item->text);
        }
        printf("\n");
        fflush(stdout);
        pthread_mutex_unlock(&q->output_mutex);
        
        free(item->text);
        free(item);
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    int n = 1;
    
    if (argc > 1) {
        n = atoi(argv[1]);
    }
    
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1 || n > num_cpus) {
        return 1;
    }
    
    pthread_t producer;
    pthread_t *consumers = (pthread_t *)malloc(n * sizeof(pthread_t));
    consumer_args_t *consumer_args = (consumer_args_t *)malloc(n * sizeof(consumer_args_t));
    
    pthread_create(&producer, NULL, producer_thread, &queue);
    
    for (int i = 0; i < n; i++) {
        consumer_args[i].thread_id = i + 1;
        consumer_args[i].queue = &queue;
        pthread_create(&consumers[i], NULL, consumer_thread, &consumer_args[i]);
    }
    
    pthread_join(producer, NULL);
    
    for (int i = 0; i < n; i++) {
        pthread_join(consumers[i], NULL);
    }
    
    free(consumers);
    free(consumer_args);
    
    int exit_code = queue.invalid_input ? 1 : 0;
    
    return exit_code;
}