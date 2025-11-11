#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

/* ============================================================================
 * KONSTANTY A ENUMERACE
 * ============================================================================ */

enum place {
    NUZKY, VRTACKA, OHYBACKA, SVARECKA, LAKOVNA, SROUBOVAK, FREZA,
    _PLACE_COUNT
};

const char *place_str[_PLACE_COUNT] = {
    [NUZKY] = "nuzky",
    [VRTACKA] = "vrtacka",
    [OHYBACKA] = "ohybacka",
    [SVARECKA] = "svarecka",
    [LAKOVNA] = "lakovna",
    [SROUBOVAK] = "sroubovak",
    [FREZA] = "freza",
};

enum product {
    A, B, C,
    _PRODUCT_COUNT
};

const char *product_str[_PRODUCT_COUNT] = {
    [A] = "A",
    [B] = "B",
    [C] = "C",
};

#define _PHASE_COUNT 6

/* Časy operací v milisekundách */
const int operation_times[_PLACE_COUNT] = {
    [NUZKY] = 100,
    [VRTACKA] = 200,
    [OHYBACKA] = 150,
    [SVARECKA] = 300,
    [LAKOVNA] = 400,
    [SROUBOVAK] = 250,
    [FREZA] = 500,
};

/* Výrobní procesy - sekvence pracovišť pro každý výrobek */
const enum place production_process[_PRODUCT_COUNT][_PHASE_COUNT] = {
    [A] = {NUZKY, VRTACKA, OHYBACKA, SVARECKA, VRTACKA, LAKOVNA},
    [B] = {VRTACKA, NUZKY, FREZA, VRTACKA, LAKOVNA, SROUBOVAK},
    [C] = {FREZA, VRTACKA, SROUBOVAK, VRTACKA, FREZA, LAKOVNA},
};

/* ============================================================================
 * DATOVÉ STRUKTURY
 * ============================================================================ */

/* Struktura pro reprezentaci dělníka */
typedef struct worker {
    char *name;                    // Jméno dělníka (dynamicky alokováno)
    enum place specialization;     // Specializace (typ pracoviště)
    pthread_t thread;              // ID vlákna
    bool should_end;               // Příznak, že má dělník ukončit práci
    bool is_working;               // Příznak, zda právě pracuje
    struct worker *next;           // Ukazatel na dalšího dělníka (spojový seznam)
} worker_t;

/* Globální struktura továrny - sdílená mezi všemi vlákny */
typedef struct {
    /* Synchronizační primitiva */
    pthread_mutex_t mutex;         // Hlavní mutex pro ochranu sdílených dat
    pthread_cond_t cond;           // Podmínková proměnná pro buzení dělníků
    
    /* Pracoviště */
    int available_places[_PLACE_COUNT];  // Počet volných pracovišť každého typu
    int total_places[_PLACE_COUNT];      // Celkový počet pracovišť každého typu
    int places_to_remove[_PLACE_COUNT];  // Počet pracovišť čekajících na odebrání
    
    /* Polotovary v jednotlivých fázích výroby */
    int parts_count[_PRODUCT_COUNT][_PHASE_COUNT];  // Počet polotovarů v každé fázi
    
    /* Seznam dělníků */
    worker_t *workers_head;        // Začátek spojového seznamu dělníků
    
    /* Příznak ukončení aplikace */
    bool shutting_down;            // Příznak, že byla zaznamenána EOF na vstupu
    
} factory_t;

/* Globální instance továrny */
factory_t factory;

void* worker_thread(void *arg);

/* ============================================================================
 * POMOCNÉ FUNKCE
 * ============================================================================ */

// find_string_in_array - Najde řetězec v poli řetězců
int find_string_in_array(const char **array, int length, char *what) {
    for (int i = 0; i < length; i++) {
        if (strcmp(array[i], what) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * get_required_place - Zjistí, jaké pracoviště je potřeba pro daný výrobek ve fázi
 * @product: Typ výrobku (A, B, C)
 * @phase: Fáze výroby (0-5)
 * 
 * Vrací: Typ pracoviště potřebný pro danou operaci
 */
enum place get_required_place(enum product product, int phase) {
    if (product < 0 || product >= _PRODUCT_COUNT) {
        fprintf(stderr, "ERROR: invalid product index %d\n", product);
        exit(201);
    }

    if (phase < 0 || phase >= _PHASE_COUNT) {
        fprintf(stderr, "ERROR: invalid phase index %d\n", phase);
        exit(202);
    }

    return production_process[product][phase];
}


// msleep - Uspí vlákno na zadaný počet milisekund
void msleep(int milliseconds) {
    if (milliseconds <= 0) return;
    sleep(milliseconds);
}

/* ============================================================================
 * FUNKCE PRO PRÁCI S DĚLNÍKY
 * ============================================================================ */

worker_t* find_worker(const char *name) {    
    worker_t *current = factory.workers_head;
    
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;  // nenasel se
}

/**
 * add_worker - Přidá nového dělníka do továrny
 * @name: Jméno dělníka (bude zkopírováno)
 * @specialization: Specializace dělníka
 * 
 * Vrací: Ukazatel na nově vytvořeného dělníka, nebo NULL při chybě
 * 
 * Vytvoří novou strukturu dělníka, spustí jeho vlákno a přidá ho do seznamu.
 */
worker_t* add_worker(const char *name, enum place specialization) {
    worker_t *worker = malloc(sizeof(worker_t));
    if (!worker) {
        fprintf(stderr, "ERROR: there was an error with allocating new worker\n");
        exit(101);
    }

    worker->name = strdup(name);
    if (!worker->name) {
        fprintf(stderr, "ERROR: there was an error with allocating name of worker\n");
        free(worker);
        exit(102);
    }
    worker->specialization = specialization;
    worker->should_end = false;
    worker->is_working = false;

    worker->next = factory.workers_head;
    factory.workers_head = worker;

    // Vytvorit thread pro noveho pracovnika
    if (pthread_create(&worker->thread, NULL, worker_thread, worker) != 0) {
        fprintf(stderr, "ERROR: Failed to create worker thread\n");
        factory.workers_head = worker->next;
        free(worker->name);
        free(worker);
        exit(102);
    }
    
    return worker;
}

// Odebere dělníka ze seznamu (po ukončení jeho vlákna) - NEVOLAT uvnitř vlákna
void remove_worker(worker_t *worker) {
    if (!worker) {
        return;
    }
    
    worker_t **current = &factory.workers_head;
    while (*current) {
        if (*current == worker) {
            *current = worker->next;
            break;
        }
        current = &(*current)->next;
    }
    
    free(worker->name);
    free(worker);
}

/* ============================================================================
 * FUNKCE PRO SPRÁVU PRÁCE
 * ============================================================================ */

/**
 * can_work - Zjistí, zda může dělník začít pracovat
 * @worker: Ukazatel na dělníka
 * @out_product: [výstup] Typ výrobku, na kterém má pracovat
 * @out_phase: [výstup] Fáze výroby
 * 
 * Vrací: true pokud existuje práce pro tohoto dělníka, false jinak
 * 
 * Hledá polotovar, který potřebuje pracoviště dané specializace.
 * Priorita: nejprve nejvyšší fáze, pak výrobek blíže začátku abecedy.
 * Poznámka: Mutex musí být zamčený před voláním!
 */
bool can_work(worker_t *worker, enum product *out_product, int *out_phase) {
    enum place spec = worker->specialization;

    if (factory.available_places[spec] <= 0) {
        // žádné volné pracoviště tohoto typu
        return false;
    }

    // Pro fáze nejdříve ty vyšší (priorita dokončení)
    for (int phase = _PHASE_COUNT - 1; phase >= 0; phase--) {
        for (int p = 0; p < _PRODUCT_COUNT; p++) {
            if (factory.parts_count[p][phase] > 0) {
                if (production_process[p][phase] == spec) {
                    // našli jsme vhodný polotovar
                    *out_product = (enum product)p;
                    *out_phase = phase;
                    return true;
                }
            }
        }
    }

    return false;
}


/**
 * finish_work - Dělník dokončí práci
 * @worker: Ukazatel na dělníka
 * @product: Typ výrobku
 * @phase: Dokončená fáze
 * 
 * Uvolní pracoviště, přesune polotovar do další fáze nebo vypíše "done".
 * Provede případné odebrání pracoviště, pokud bylo naplánováno.
 * Poznámka: Mutex musí být zamčený před voláním!
 */
void finish_work(worker_t *worker, enum product product, int phase) {
    enum place spec = worker->specialization;

    // Pokud byla poslední fáze -> done
    if (phase == _PHASE_COUNT - 1) {
        printf("done %s\n", product_str[product]);
        fflush(stdout);
    } else {
        // Jinak přesunout polotovar do další fáze
        factory.parts_count[product][phase + 1]++;
    }

    // Uvolnit pracoviště, ale pokud je plánované odebrání, odebereme ho
    if (factory.places_to_remove[spec] > 0) {
        // Odebereme jedno plánované místo
        factory.places_to_remove[spec]--;
        if (factory.total_places[spec] > 0)
            factory.total_places[spec]--;
        // dostupné místo nezvyšujeme, protože ho právě odebíráme
    } else {
        factory.available_places[spec]++;
    }

    // Dělník už není zaneprázdněn
    worker->is_working = false;

    // Probudit čekající dělníky (i hlavní vlákno pokud čeká)
    pthread_cond_broadcast(&factory.cond);
}

/**
 * start_work - Dělník začne pracovat na polotovaru
 * @worker: Ukazatel na dělníka
 * @product: Typ výrobku
 * @phase: Fáze výroby
 * 
 * Odebere polotovar z fronty, zarezervuje pracoviště, vypíše informaci
 * o zahájení práce, uspí vlákno na dobu operace.
 * Poznámka: Mutex musí být zamčený před voláním (a je odemčený při čekání)!
 */
void start_work(worker_t *worker, enum product product, int phase) {
    enum place spec = worker->specialization;

    // Odebrat polotovar z fáze
    if (factory.parts_count[product][phase] <= 0) {
        // nic tam není (možná mezistav) - měl by být ošetřen před voláním
        return;
    }
    factory.parts_count[product][phase]--;
    // Rezervovat pracoviště
    factory.available_places[spec]--;
    worker->is_working = true;

    // Vypis aktivity (fáze se tiskne 1..6)
    // Tisk provádíme zatím při zamčeném mutexu, aby se udrželo pořadí
    printf("%s %s %d %s\n", worker->name, place_str[spec], phase + 1, product_str[product]);
    fflush(stdout);

    // Uvolnit mutex během spánku - práce běží asynchronně
    pthread_mutex_unlock(&factory.mutex);

    // Provedení operace (uspání)
    msleep(operation_times[spec]);

    // Po vykonání práce si znovu zamkneme mutex a dokončíme práci
    pthread_mutex_lock(&factory.mutex);

    finish_work(worker, product, phase);
}


/* ============================================================================
 * FUNKCE PRO UKONČOVÁNÍ
 * ============================================================================ */

/**
 * is_profession_needed - Zjistí, zda je daná profese ještě potřeba
 * @specialization: Typ pracoviště (specializace)
 * 
 * Vrací: true pokud existují polotovary, které budou potřebovat tuto profesi
 * 
 * Poznámka: Mutex musí být zamčený před voláním!
 */
bool is_profession_needed(enum place specialization) {
    // Pro všechny polotovary zkontrolujeme, zda v některé současné
    // nebo budoucí fázi bude potřeba tato specializace.
    for (int p = 0; p < _PRODUCT_COUNT; p++) {
        for (int phase = 0; phase < _PHASE_COUNT; phase++) {
            if (factory.parts_count[p][phase] > 0) {
                // podívat se, jestli v některé fázi >= phase je tato specializace
                for (int future = phase; future < _PHASE_COUNT; future++) {
                    if (production_process[p][future] == specialization) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/**
 * should_worker_terminate - Zjistí, zda má dělník ukončit své vlákno
 * @worker: Ukazatel na dělníka
 * 
 * Vrací: true pokud má dělník odejít
 * 
 * Dělník odchází, pokud:
 * - Byl vydán příkaz "end" pro tohoto dělníka
 * - Aplikace se ukončuje (EOF) a jeho profese už není potřeba
 * Poznámka: Mutex musí být zamčený před voláním!
 */
bool should_worker_terminate(worker_t *worker) {
    if (worker->should_end && !worker->is_working) {
        return true;
    }

    if (factory.shutting_down && !worker->is_working) {
        // Pokud při ukončování už jeho profese není potřeba, může odejít
        if (!is_profession_needed(worker->specialization)) {
            return true;
        }
    }

    return false;
}

/* ============================================================================
 * HLAVNÍ FUNKCE VLÁKNA DĚLNÍKA
 * ============================================================================ */

/**
 * worker_thread - Hlavní funkce vlákna dělníka
 * @arg: Ukazatel na strukturu worker_t
 * 
 * Vrací: NULL při ukončení
 * 
 * Hlavní smyčka dělníka:
 */
void* worker_thread(void *arg) {
    worker_t *self = (worker_t*)arg;

    pthread_mutex_lock(&factory.mutex);

    while (1) {
        // Pokud má odejít (a není zaneprázdněn), odejde
        if (should_worker_terminate(self)) {
            // Vypíše goes home a ukončí se
            printf("%s goes home\n", self->name);
            fflush(stdout);
            pthread_mutex_unlock(&factory.mutex);
            return NULL;
        }

        // Zkus najít práci
        enum product prod;
        int phase;
        if (can_work(self, &prod, &phase)) {
            // Spustit práci (start_work odemkne, uspí a zase zamkne)
            start_work(self, prod, phase);
            // po návratu z start_work() jsme s mutexem zamčeni
            continue; // opakovat hledání práce / ukončení
        }

        // Nic pro něj není -> čeká na signál
        pthread_cond_wait(&factory.cond, &factory.mutex);
        // po probuzení se vrátí vrchol while a znovu zkontroluje podmínky
    }

    // unreachable
    pthread_mutex_unlock(&factory.mutex);
    return NULL;
}

/* ============================================================================
 * FUNKCE PRO SPRÁVU TOVÁRNY
 * ============================================================================ */

/**
 * factory_init - Inicializuje továrnu
 */
void factory_init(void) {
    memset(&factory, 0, sizeof(factory_t));

    // Inicializace mutexu a podmínkové proměnné
    if (pthread_mutex_init(&factory.mutex, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize mutex\n");
        exit(201);
    }

    if (pthread_cond_init(&factory.cond, NULL) != 0) {
        fprintf(stderr, "ERROR: Failed to initialize condition variable\n");
        pthread_mutex_destroy(&factory.mutex);
        exit(202);
    }

    factory.workers_head = NULL;
    factory.shutting_down = false;
}

/**
 * factory_destroy - Uvolní zdroje továrny
 */
void factory_destroy(void) {
    // Zničí mutex a podmínkovou proměnnou
    if (pthread_mutex_destroy(&factory.mutex) != 0) {
        fprintf(stderr, "WARNING: Failed to destroy mutex (might still be locked)\n");
    }

    if (pthread_cond_destroy(&factory.cond) != 0) {
        fprintf(stderr, "WARNING: Failed to destroy condition variable\n");
    }

    // Pro jistotu – kdyby nějaký dělník nebyl uvolněn (bezpečnostní úklid)
    worker_t *w = factory.workers_head;
    while (w) {
        worker_t *next = w->next;
        free(w->name);
        free(w);
        w = next;
    }
    factory.workers_head = NULL;

    // Továrna už není aktivní
    factory.shutting_down = true;
}

/**
 * factory_add_product - Přidá požadavek na výrobu výrobku
 * @product: Typ výrobku
 * 
 * Poznámka: Mutex musí být zamčený před voláním!
 */
void factory_add_product(enum product product) {
    // přidat do fáze 0
    factory.parts_count[product][0]++;
}

/**
 * factory_add_place - Přidá nové pracoviště
 * @place: Typ pracoviště
 * 
 * Poznámka: Mutex musí být zamčený před voláním!
 */
void factory_add_place(enum place place) {
    factory.total_places[place]++;
    factory.available_places[place]++;
}

/**
 * factory_remove_place - Naplánuje odebrání pracoviště
 * @place: Typ pracoviště
 * 
 * Poznámka: Mutex musí být zamčený před voláním!
 */
void factory_remove_place(enum place place) {
    // Pokud je volné pracoviště, odebrat ho okamžitě
    if (factory.available_places[place] > 0) {
        factory.available_places[place]--;
        if (factory.total_places[place] > 0)
            factory.total_places[place]--;
    } else {
        // Jinak naplánovat odebrání po dokončení některé probíhající operace
        factory.places_to_remove[place]++;
    }
}

/**
 * factory_end_worker - Naplánuje ukončení dělníka podle jména
 * @name: Jméno dělníka
 * 
 * Poznámka: Mutex musí být zamčený před voláním!
 */
void factory_end_worker(const char *name) {
    worker_t *w = find_worker(name);
    if (!w) {
        // neexistuje - ignoruj
        return;
    }
    // pokud je již nastaven should_end, nic neděláme
    w->should_end = true;
}

/**
 * factory_shutdown - Zahájí proces ukončování továrny
 * 
 * Poznámka: Mutex musí být zamčený před voláním!
 */
void factory_shutdown(void) {
    factory.shutting_down = true;
    // probudit všechny pracovníky: provedeno v main pomocí broadcastu
}

/**
 * factory_wait_for_workers - Počká na ukončení všech dělníků
 * 
 * Projde seznam dělníků a zavolá pthread_join pro každého.
 * Poté uvolní paměť alokovanou pro dělníky.
 */
void factory_wait_for_workers(void) {
    worker_t *worker = factory.workers_head;
    
    while (worker) {
        worker_t *next = worker->next;
        
        pthread_join(worker->thread, NULL);
        
        free(worker->name);
        free(worker);
        
        worker = next;
    }
    
    factory.workers_head = NULL;
}

/* ============================================================================
 * HLAVNÍ FUNKCE
 * ============================================================================ */

int main(int argc, char **argv)
{
    factory_init();
    
    char *line = NULL;
    size_t sz = 0;
    
    while (1) {
        char *cmd, *arg1, *arg2, *arg3, *saveptr;

        if (getline(&line, &sz, stdin) == -1)
            break; /* Error or EOF */

        cmd = strtok_r(line, " \r\n", &saveptr);
        arg1 = strtok_r(NULL, " \r\n", &saveptr);
        arg2 = strtok_r(NULL, " \r\n", &saveptr);
        arg3 = strtok_r(NULL, " \r\n", &saveptr);

        if (!cmd) {
            continue; /* Empty line */
        } else if (strcmp(cmd, "start") == 0 && arg1 && arg2 && !arg3) {
            int place = find_string_in_array(place_str, _PLACE_COUNT, arg2);
            if (place >= 0) {
                pthread_mutex_lock(&factory.mutex);
                // nepřijde dva dělníci se stejným jménem podle zadání, ale kontrola neuškodí
                if (find_worker(arg1) != NULL) {
                    fprintf(stderr, "Worker with name %s already exists\n", arg1);
                    pthread_mutex_unlock(&factory.mutex);
                } else {
                    add_worker(arg1, place);
                    pthread_cond_broadcast(&factory.cond);
                    pthread_mutex_unlock(&factory.mutex);
                }
            } else {
                fprintf(stderr, "Invalid place: %s\n", arg2);
            }
        } else if (strcmp(cmd, "make") == 0 && arg1 && !arg2) {
            int product = find_string_in_array(product_str, _PRODUCT_COUNT, arg1);
            if (product >= 0) {
                pthread_mutex_lock(&factory.mutex);
                factory_add_product(product);
                pthread_cond_broadcast(&factory.cond);
                pthread_mutex_unlock(&factory.mutex);
            } else {
                fprintf(stderr, "Invalid product: %s\n", arg1);
            }
        } else if (strcmp(cmd, "end") == 0 && arg1 && !arg2) {
            pthread_mutex_lock(&factory.mutex);
            factory_end_worker(arg1);
            pthread_cond_broadcast(&factory.cond);
            pthread_mutex_unlock(&factory.mutex);
        } else if (strcmp(cmd, "add") == 0 && arg1 && !arg2) {
            int place = find_string_in_array(place_str, _PLACE_COUNT, arg1);
            if (place >= 0) {
                pthread_mutex_lock(&factory.mutex);
                factory_add_place(place);
                pthread_cond_broadcast(&factory.cond);
                pthread_mutex_unlock(&factory.mutex);
            } else {
                fprintf(stderr, "Invalid place: %s\n", arg1);
            }
        } else if (strcmp(cmd, "remove") == 0 && arg1 && !arg2) {
            int place = find_string_in_array(place_str, _PLACE_COUNT, arg1);
            if (place >= 0) {
                pthread_mutex_lock(&factory.mutex);
                factory_remove_place(place);
                pthread_mutex_unlock(&factory.mutex);
            } else {
                fprintf(stderr, "Invalid place: %s\n", arg1);
            }
        } else {
            fprintf(stderr, "Invalid command: %s\n", line);
        }
    }
    
    free(line);
    
    /* Zahájit ukončování */
    pthread_mutex_lock(&factory.mutex);
    factory_shutdown();
    pthread_cond_broadcast(&factory.cond);
    pthread_mutex_unlock(&factory.mutex);
    
    /* Počkat na všechny dělníky */
    factory_wait_for_workers();
    
    /* Uklidit */
    factory_destroy();
    
    return 0;
}
