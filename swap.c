// ISMAEL BREA ARIAS 
// OSCAR ÁLVAREZ VIDAL
// GRUPO 4.1

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "op_count.h"
#include "options.h"

struct buffer {
    int *data;
    int size;
    pthread_mutex_t *mutexes; // Array de mutexes, uno por cada posición del buffer
    pthread_mutex_t print_mutex; // Mutex adicional para la impresión
};

struct thread_info {
    pthread_t       thread_id;        // id returned by pthread_create()
    int             thread_num;       // application defined thread #
};

struct shared_data {                // shared data between threads (including constant iterations and a mutex to protect it)
    volatile int iterations;      
    pthread_mutex_t mutex;
};

struct args {
    int             thread_num;       // application defined thread #
    int             delay;            // delay between operations
    struct buffer   *buffer;          // Shared buffer
    int             print_wait;       // delay between prints of the array
    struct shared_data *shared;       // shared data including iterations
};

void print_buffer(struct buffer *buffer);

void *swap(void *ptr){
    struct args *args =  ptr;

    while(1) {
        int i, j, tmp;
        
        // Verifica si quedan iteraciones
        pthread_mutex_lock(&args->shared->mutex);
        if (args->shared->iterations <= 0) {
            pthread_mutex_unlock(&args->shared->mutex);
            break;
        }
        args->shared->iterations--;
        pthread_mutex_unlock(&args->shared->mutex);

        i = rand() % args->buffer->size;
        j = rand() % args->buffer->size;

        // Para evitar interbloqueos, siempre bloqueamos el mutex con el índice más pequeño primero
        if (i == j) {
            // Si i es igual a j, no hacemos swap y mantenemos las iteraciones constantes
            args->shared->iterations++;
            continue;
        }
        else if (i < j)
        {
            pthread_mutex_lock(&args->buffer->mutexes[i]);
            pthread_mutex_lock(&args->buffer->mutexes[j]);
        }
        else
        {
            pthread_mutex_lock(&args->buffer->mutexes[j]);
            pthread_mutex_lock(&args->buffer->mutexes[i]);
        }

        pthread_mutex_lock(&args->buffer->print_mutex);
        printf("Thread %d swapping positions %d (== %d) and %d (== %d)\n",
            args->thread_num, i, args->buffer->data[i], j, args->buffer->data[j]);
        pthread_mutex_unlock(&args->buffer->print_mutex);

        tmp = args->buffer->data[i];
        if(args->delay) usleep(args->delay); // Force a context switch

        args->buffer->data[i] = args->buffer->data[j];
        if(args->delay) usleep(args->delay);

        args->buffer->data[j] = tmp;
        if(args->delay) usleep(args->delay);

        // Desbloquear los mutexes en el orden inverso al que se bloquearon
        if (i < j) {
            pthread_mutex_unlock(&args->buffer->mutexes[j]);
            pthread_mutex_unlock(&args->buffer->mutexes[i]);
        } else {
            pthread_mutex_unlock(&args->buffer->mutexes[i]);
            pthread_mutex_unlock(&args->buffer->mutexes[j]);
        }

        inc_count();
    }
    return NULL;
}

void *print_thread(void *ptr) {
    struct args *args = ptr;
    while (1) {
        usleep(args->print_wait * 1000); // Convertir ms a us
        pthread_mutex_lock(&args->buffer->print_mutex);
        printf("Current buffer state: ");
        print_buffer(args->buffer);
        pthread_mutex_unlock(&args->buffer->print_mutex);
    }
    return NULL;
}

int cmp(const void *e1, const void *e2) {
    int a = *(int *)e1;
    int b = *(int *)e2;
    return (a > b) - (a < b);
}

void print_buffer(struct buffer *buffer) {
    for (int i = 0; i < buffer->size; i++)
        printf("%i ", buffer->data[i]);
    printf("\n");
}

void start_threads(struct options opt) {
    int i;
    struct thread_info *threads;
    struct args *args;
    struct buffer buffer;
    struct shared_data shared;

    srand(time(NULL));

    if((buffer.data = malloc(opt.buffer_size * sizeof(int))) == NULL) {
        printf("Out of memory\n");
        exit(1);
    }
    buffer.size = opt.buffer_size;

    // Inicializar el array de mutexes
    buffer.mutexes = malloc(sizeof(pthread_mutex_t) * opt.buffer_size);
    if (buffer.mutexes == NULL) {
        printf("Out of memory\n");
        exit(1);
    }
    for (i = 0; i < opt.buffer_size; i++) {
        if (pthread_mutex_init(&buffer.mutexes[i], NULL) != 0) {
            printf("Mutex initialization failed\n");
            exit(1);
        }
    }

    // Inicializar el mutex de impresión
    if (pthread_mutex_init(&buffer.print_mutex, NULL) != 0) {
        printf("Print mutex initialization failed\n");
        exit(1);
    }

    // Inicializar shared data
    shared.iterations = opt.iterations;
    if (pthread_mutex_init(&shared.mutex, NULL) != 0) {
        printf("Shared data mutex initialization failed\n");
        exit(1);
    }

    for(i = 0; i < buffer.size; i++)
        buffer.data[i] = i;

    printf("creating %d threads\n", opt.num_threads);
    threads = malloc(sizeof(struct thread_info) * opt.num_threads);
    args = malloc(sizeof(struct args) * opt.num_threads);

    if (threads == NULL || args == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    printf("Buffer before: ");
    print_buffer(&buffer);

    // Crear los threads de swap
    for (i = 0; i < opt.num_threads; i++) {
        threads[i].thread_num = i;

        args[i].thread_num = i;
        args[i].buffer     = &buffer;
        args[i].delay      = opt.delay;
        args[i].shared     = &shared;

        if (pthread_create(&threads[i].thread_id, NULL, swap, &args[i]) != 0) {
            printf("Could not create thread #%d\n", i);
            exit(1);
        }
    }

    // Crear el thread de impresión
    pthread_t print_thread_id;
    struct args print_args;
    print_args.buffer = &buffer;
    print_args.print_wait = opt.print_wait;

    if (pthread_create(&print_thread_id, NULL, print_thread, &print_args) != 0) {
        printf("Could not create print thread\n");
        exit(1);
    }

    // Esperar a que los threads terminen
    for (i = 0; i < opt.num_threads; i++)
        pthread_join(threads[i].thread_id, NULL);

    // Cancelar y unir el thread de impresión
    pthread_cancel(print_thread_id);
    pthread_join(print_thread_id, NULL);

    // Imprimir el buffer final
    printf("Buffer after: ");
    print_buffer(&buffer);

    printf("Buffer after swapping (sorted): ");
    qsort(buffer.data, opt.buffer_size, sizeof(int), cmp);
    print_buffer(&buffer);

    printf("Iterations: %d\n", get_count());

    // Liberar memoria
    free(args);
    free(threads);
    free(buffer.data);

    // Destruir los mutexes
    for (i = 0; i < opt.buffer_size; i++) {
        pthread_mutex_destroy(&buffer.mutexes[i]);
    }
    free(buffer.mutexes);

    // Destruir el mutex de impresión
    pthread_mutex_destroy(&buffer.print_mutex);

    // Destruir el mutex de shared data
    pthread_mutex_destroy(&shared.mutex);

    pthread_exit(NULL);
}

int main (int argc, char **argv) {
    struct options opt;

    // Valores por defecto
    opt.num_threads = 10;
    opt.buffer_size = 10;
    opt.iterations  = 100;
    opt.delay       = 10;
    opt.print_wait  = 1000; // Default a 1 segundo

    read_options(argc, argv, &opt);

    start_threads(opt);

    exit(0);
}