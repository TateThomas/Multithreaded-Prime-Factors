#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_BUFFER_SIZE (10)
#define INIT_VECTOR_SIZE (10)
#define NUM_PRODUCERS (1)
#define NUM_CONSUMERS (1)


typedef struct{
	int** buffer;
	int next_in;
	int next_out;
	int length;
	pthread_mutex_t mutex;
	pthread_cond_t added, removed;
} Buffer;

void buffer_init(Buffer* buff) {

	// allocate space for new buffer
	int** arr;
	arr = (int**)calloc(MAX_BUFFER_SIZE, sizeof(int*));
	buff->buffer = arr;

	buff->next_in = 0;
	buff->next_out = -1;
	buff->length = 0;

	pthread_mutex_init(&buff->mutex, NULL);
	pthread_cond_init(&buff->added, NULL);
	pthread_cond_init(&buff->removed, NULL);
}

void buffer_add(Buffer* buff, int* num, size_t size) {

	buff->buffer[buff->next_in] = (int*)malloc(size); 	// allocate new space for buffer entry
	memcpy(buff->buffer[buff->next_in], num, size); 	// copy data into buffer
	free(num); 	// free old allocated data

	buff->next_in = (buff->next_in + 1) % MAX_BUFFER_SIZE; 	// increment next_in
	buff->length++;

	if (buff->length == 1) {
		buff->next_out = 0; 	// used to be empty, so next_out used to be -1, so initialize it to new data
	}
}

void buffer_remove(Buffer* buff, int** num) {

	// find length of buffer data for allocation
	int len = 0;
	for (int i = 0; buff->buffer[buff->next_out][i] > 0; i++) {
		len++;
	}
	len++;

	*num = (int*)malloc(len * sizeof(int)); 	// allocate space for dest
	memcpy(*num, buff->buffer[buff->next_out], (size_t)(len * sizeof(int))); 	// copy buffer data into dest
	free(buff->buffer[buff->next_out]); 	// free old allocated buffer data

	buff->next_out = (buff->next_out + 1) % MAX_BUFFER_SIZE; 	// increment next_out
	buff->length--;

	if (buff->next_in == buff->next_out) {
		// buffer is now empty, move indexes to initial values
		buff->next_in = 0;
		buff->next_out = -1;
	}
}


typedef struct{
	int* nums;
	int length;
	int size;
	pthread_mutex_t mutex;
} Vector;

void vector_init(Vector* v) {

	// allocate space for new array
	int* arr;
	arr = (int*)calloc(INIT_VECTOR_SIZE, sizeof(int));
	v->nums = arr;

	v->length = 0;
	v->size = INIT_VECTOR_SIZE;

	pthread_mutex_init(&v->mutex, NULL);
}

void vector_add(Vector* v, int num) {

	if (v->length == (v->size - 1)) {
		// array will be full after appending, allocate new array
		v->size *= 2; 	// new size is double of the old size
		int* arr;
		arr = (int*)calloc(v->size, sizeof(int));
		memcpy(arr, v->nums, (sizeof(int) * v->length));
		free(v->nums); 	// free old array
		v->nums = arr; 	// point to new array
	}

	// append new value
	v->nums[v->length] = num;
	v->length++;
}


typedef struct {
        Buffer* numbers;
        Buffer* factors;
        Vector* primes;
        int num_done, fact_done, print_done;
	pthread_mutex_t mutex;
} Arguments;

void args_init(Arguments* args, Buffer* nums, Buffer* facts, Vector* primes) {

	args->numbers = nums;
	args->factors = facts;
	args->primes = primes;

	args->num_done = 0;
	args->fact_done = 0;
	args->print_done = 0;

	pthread_mutex_init(&args->mutex, NULL);
}


void find_prime_factors(Vector* primes, Vector* factors) {

        int is_prime = 1, value = factors->nums[0], index;

        for (int i = 2; value > 1; i++) {

                // check i to see if it is prime or already in primes array
                for (int j = 0; j < primes->length; j++) {
                        if ((i % primes->nums[j]) == 0) {
                                if (i != primes->nums[j]) is_prime = -1;        // not prime
                                else is_prime = 0;      // is prime, but already in array
                                break;
                        }
			index = j;
                }

                if (is_prime == -1) {
                        is_prime = 1;
                }
                else {
                        // if i is a new prime, add it to the primes array
                        if (is_prime == 1) {
				// lock mutex to safely append to primes vector
				pthread_mutex_lock(&primes->mutex);
                                if (primes->length == index) { 	// check if another process added that value while waiting
					vector_add(primes, i);
				}
				pthread_mutex_unlock(&primes->mutex);
                        }
                        // continually divide by prime until the value isnt divisible anymore
                        while ((value % i) == 0) {
                                vector_add(factors, i);
                                value /= i;
                        }
                }
                if (i != 2) i++;
        }
        vector_add(factors, 0);        // factors array is 0 terminated
}


void produce(Buffer* nums, Buffer* factors, Vector* primes, int* num_done) {

	// aquire mutex from factors buffer
	pthread_mutex_lock(&nums->mutex);
	while (nums->length == 0) {
		if (*num_done) {
			// length is 0 and main thread is done, so leave
			pthread_mutex_unlock(&nums->mutex);
			return;
		}
		pthread_cond_wait(&nums->added, &nums->mutex);
	}

	// retrieve data from buffer
	int* num;
	buffer_remove(nums, &num);

	// unlock mutex and signal that data was removed
	pthread_mutex_unlock(&nums->mutex);
	pthread_cond_signal(&nums->removed);

	// create new vector to hold number and prime factors
	Vector* arr;
	arr = (Vector*)malloc(sizeof(Vector)); 	// allocate new vector
	vector_init(arr);
	vector_add(arr, *num); 	// append number desired to be factorized
	free(num); 	// free data allocated to hold data from buffer

	find_prime_factors(primes, arr);

	// aquire mutex from factors buffer
	pthread_mutex_lock(&factors->mutex);
	while (factors->length == (MAX_BUFFER_SIZE - 1)) {
		pthread_cond_wait(&factors->removed, &factors->mutex);
	}

	// add prime factors array to buffer
	buffer_add(factors, arr->nums, (size_t)(arr->length * sizeof(int)));

	// unlock mutex and signal that data was added
	pthread_mutex_unlock(&factors->mutex);
	pthread_cond_signal(&factors->added);

	free(arr); 	// free vector since the new array has now been added to buffer
	// note: actual array in Vector struct is deallocated in buffer_add function
}


void consume(Buffer* factors, int* fact_done) {

	// aquire mutex from factors buffer
	pthread_mutex_lock(&factors->mutex);
	while(factors->length == 0) {
		if (*fact_done) {
			// length is 0 and producer is done, so leave
			pthread_mutex_unlock(&factors->mutex);
			return;
		}
		pthread_cond_wait(&factors->added, &factors->mutex);
	}

	// retrieve data from buffer
	int* arr;
	buffer_remove(factors, &arr);

	// unlcok mutex and signal that data was removed
	pthread_mutex_unlock(&factors->mutex);
	pthread_cond_signal(&factors->removed);

	// make string for final output, so it can be printed all at once to prevent inteference
	char result[128];
	char number[64];
	snprintf(result, sizeof(result), "%d:", arr[0]);
	for (int i = 1; arr[i] > 0; i++) {
		snprintf(number, sizeof(number), " %d", arr[i]);
		strcat(result, number);
	}
	printf("%s\n", result);

	free(arr); 	// free array to hold data from buffer data
}


void* producer(void* ptr) {

	Arguments* args;
	args = (Arguments *)ptr;

	// produce while main thread isnt done and while there is still data in buffer
	while ((args->num_done == 0) || (args->numbers->length > 0)) {
		produce(args->numbers, args->factors, args->primes, &args->num_done);
	}

	// aquire mutex to increment amount of producer threads done
	pthread_mutex_lock(&args->mutex);
	args->fact_done++;
	pthread_mutex_unlock(&args->mutex);

	// ensure that no threads are stuck until all have exited
	while (args->fact_done < NUM_PRODUCERS) {
		pthread_cond_signal(&args->numbers->added);
	}
}


void* consumer(void* ptr) {

	Arguments* args;
	args = (Arguments *)ptr;

	// consume while producer threads arent done and while there is still data in buffer
	while ((args->fact_done < NUM_PRODUCERS) || (args->factors->length > 0)) {
		consume(args->factors, &args->fact_done);
	}

	// aquire mutex to increment amount of consumer threads done
	pthread_mutex_lock(&args->mutex);
	args->print_done++;
	pthread_mutex_unlock(&args->mutex);

	// ensure that no threads are stuck until all have exited
	while (args->print_done < NUM_CONSUMERS) {
		pthread_cond_signal(&args->factors->added);
	}
}


int main(int argc, char** argv) {

	if (argc == 1) {
		// no arguments given
		printf("Usage: $%s <number to factor>...\n", argv[0]);
		return 0;
	}

	// allocate and initialize vector of prime numbers
	Vector* primes;
	primes = (Vector*)malloc(sizeof(Vector));
	vector_init(primes);

	// allocate and initialize buffer for communication between main and producer threads
	Buffer* numbers;
	numbers = (Buffer*)malloc(sizeof(Buffer));
	buffer_init(numbers);

	// allocate and initialize buffer for commmunication between producer and consumer threads
	Buffer* factors;
	factors = (Buffer*)malloc(sizeof(Buffer));
	buffer_init(factors);

	// create and initialize struct to pass arguments through pthread_create
	Arguments args;
	args_init(&args, numbers, factors, primes); 	// note: all threads will use the same arguments

	int* num;

	pthread_t* prod;
	pthread_t* prod_arr[NUM_PRODUCERS]; 	// array to keep track of producer threads

	pthread_t* cons;
	pthread_t* cons_arr[NUM_CONSUMERS]; 	// array to keep track of consumer threads

	// start producer threads
	for (int i = 0; i < NUM_PRODUCERS; i++) {
		prod = (pthread_t*)malloc(sizeof(pthread_t));
		prod_arr[i] = prod;
		pthread_create(prod, NULL, producer, (void *)&args);
	}

	// start consumer threads
	for (int i = 0; i < NUM_CONSUMERS; i++) {
                cons = (pthread_t*)malloc(sizeof(pthread_t));
                cons_arr[i] = cons;
                pthread_create(cons, NULL, consumer, (void *)&args);
        }

	// main process
	for (int i = 1; i < argc; i++) {

		// aquire mutex lock for numbers buffer
		pthread_mutex_lock(&args.numbers->mutex);
		while (args.numbers->length == (MAX_BUFFER_SIZE - 1)) {
			pthread_cond_wait(&numbers->removed, &numbers->mutex);
		}

		// allocate new memory to insert number in buffer
		num = (int*)malloc(sizeof(int) * 2);
		*num = atoi(argv[i]);
		buffer_add(numbers, num, (sizeof(int) * 2));

		// unlock mutex and signal that data has been added
		pthread_mutex_unlock(&numbers->mutex);
		pthread_cond_signal(&numbers->added);
	}
	args.num_done++; 	// note: no mutex lock needed since theres only 1 main thread always

	// join all producer threads
	for (int i = 0; i < NUM_PRODUCERS; i++) {
		pthread_cond_signal(&numbers->added); 	// once again, ensure no processes are stuck
		pthread_join(*(prod_arr[i]), NULL);
		free(prod_arr[i]); 	// free memory allocated to hold pthread handle
	}

	// join all producer threads
	for (int i = 0; i < NUM_CONSUMERS; i++) {
		pthread_cond_signal(&factors->added); 	// once again, ensure no processes are stuck
                pthread_join(*(cons_arr[i]), NULL);
                free(cons_arr[i]); 	// free memory allocated to hold pthread handle
        }

	//free other allocated data

	free(primes->nums);
	free(primes);

	free(numbers->buffer);
	free(numbers);

	free(factors->buffer);
	free(factors);

	return 0;
}
