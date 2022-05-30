#include "uthreads.h"
#include <iostream>
#include <stdio.h>
#include <deque>
#include <map>
#include <queue>
#include <armadillo>
#include <csetjmp>
#include <signal.h>

typedef unsigned long address_t;
void thread_switch(int sig);
int make_timer();
void update_sleep_list(int tid);
void update_ready_list(int tid);
void handle_running();
int getNextId();
char *create_env(thread_entry_point entry_point, __jmp_buf_tag *env);
int timer_sig();

#define JB_SP 6
#define JB_PC 7


#define SYSTEM_ERR "system error: "
#define THREAD_LIBRARY_ERR "thread library error: "
#define SIGACTION_ERR "sigaction error."
#define SETITIMER_ERR "setitimer error."
#define QUANTUM_POSITIVE_ERR "quantum input is non positive."
#define REACHED_MAX_THREADS_ERR "reached the max threads amount."
#define ID_NOT_EXISTS_ERR "thread id doesn't exists"
#define BLOCK_MAIN_THREAD_ERR "can't block main thread"
#define BLOCK_MAIN_THREAD_SLEEP_ERR "main thread id can't call this function"
#define NUM_QUANTUM_NEG_ERR "sleeping counter is negative"
#define BLOCKED_SIG "blocked signal error."
#define UNBLOCKED_SIG "unblocked signal error."
#define INIT_CALLED_MORE_THAN_ONCE "init can't be called more then once"
#define ENTRY_POINT_NULL "entry point cant be null"
#define ALLOC_ERR "allocation error"
#define SLEEP_WHEN_RUNNING_ERR "can't sleep if not in running state"

enum state {RUNNING, READY, BLOCKED};

int quantum_micros;
std::deque<int> ready_list;
std::deque<int> sleep_list;
std::priority_queue<int,std::vector<int>,std::greater<int>> ids_free_from_terminate;

struct itimerval timer;
unsigned int quantum_counter;
int max_id_used;
int running_id;
sigset_t signal_set;

/**
 *
 * the thread struct.
 *
*/
struct thread {
    
    state thread_state;
    int thread_quantum_running_counter;
    int sleep_time_left;
    sigjmp_buf env;
    bool resumed;
    char* stack;
};

std::map<int, thread*> all_threads;

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

/**
 *
 * this function blocks signals with sigprocmask of the calling thread.
 *
 * @return 0 on success, return 0. On failure, exits with -1.
*/
int block_signal() {
    if (sigprocmask(SIG_BLOCK,&signal_set,NULL)) {
        std::cerr << SYSTEM_ERR << BLOCKED_SIG << std::endl;
        exit(-1);
    }
    return 0;
}

/**
 *
 * this function unblocks signals with sigprocmask of the calling thread.
 *
 * @return 0 on success. On failure, exits with -1.
*/
int unblock_signal() {
    if (sigprocmask(SIG_UNBLOCK,&signal_set,NULL)) {
        std::cerr << SYSTEM_ERR << UNBLOCKED_SIG << std::endl;
        exit(-1);
    }
    return 0;
}

/**
 *
 * This function checks if the sleeping threads are finished their sleep time. If
 * so and they are resumed, it wakes them up, by placing them in the ready list.
 * If it just blockes but the sleeping time is over, it just keeps them blocked
 * but doesnt add them to the new sleep list. Otherwise, it add them to the new sleeping list.
 *
*/
void handle_sleep() {
    std::deque<int> new_sleep_list;

    for (int thread_id : sleep_list) {
        thread* current_thread = all_threads[thread_id];
        if (current_thread->sleep_time_left > 0) {
            current_thread->sleep_time_left--;
            if (current_thread->sleep_time_left > 0) {
                new_sleep_list.push_back(thread_id);
            } else {
                if (current_thread->resumed){
                    current_thread->thread_state = READY;
                    ready_list.push_back(thread_id);
                }
            }
        }
        else if (current_thread->resumed){
            current_thread->thread_state = READY;
            ready_list.push_back(thread_id);
        }
    }
    sleep_list = new_sleep_list;
}

/**
 *
 * this function sets the virtual timer (decremented when the process is running)
 *
 * @return On success, return 0. On failure, returns -1.
*/
int timer_sig() {
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        std::cerr << SYSTEM_ERR<<SETITIMER_ERR  << std::endl;
        return -1;
    }
    return 0;
}

/**
 *
 * This function is being called after a quantum passes. It switches between threads by
 * placing the current running thread in the end of the ready list and places the next 
 * ready thread to run on the CPU.
 *
*/
void thread_switch(int sig)
{
    block_signal();
    quantum_counter++;
    handle_sleep();
    if (ready_list.empty()) {
        all_threads[running_id]->thread_quantum_running_counter++;
        timer_sig();
        unblock_signal();
        return;
    }
    if (sigsetjmp(all_threads[running_id]->env, 1) != 0) {
        timer_sig();
        return;
    }

    // move from RUNNING state to READY state
    if (sig!=1){
        ready_list.push_back(running_id);
        all_threads[running_id]->thread_state = READY;
    }
    int next_thread_id = ready_list.front();
    all_threads[next_thread_id]->thread_quantum_running_counter++;
    ready_list.pop_front();
    all_threads[next_thread_id]->thread_state = RUNNING;
    running_id = next_thread_id;
    // remove from sleep if counted expired
    unblock_signal();
    siglongjmp(all_threads[next_thread_id]->env, 1);
}

/**
 *
 * this function creates the timer.
 * update the state of the running_id to be running and initialize all the timer configurations needed.
 * Install thread_switch as the signal handler for SIGVTALRM and sets the timer.
 *
 * @return On success, return 0. On failure, returns -1.
*/
int make_timer() {
    all_threads[running_id]->thread_state = RUNNING;
    struct sigaction sa = {0};

    // Install thread_switch as the signal handler for SIGVTALRM.
    sa.sa_handler = &thread_switch;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        std::cerr << SYSTEM_ERR << SIGACTION_ERR << std::endl;
        return -1;
    }
    // Configure the timer to expire after quantum_micros microseconds... */
    timer.it_value.tv_sec = (int)(quantum_micros/1000000);        // first time interval, seconds part
    timer.it_value.tv_usec = (int)(quantum_micros%1000000);        // first time interval, microseconds part

    // Configure the timer to expire after quantum_micros microseconds... */
    timer.it_interval.tv_sec = (int)(quantum_micros/1000000);        // following time intervals, seconds part
    timer.it_interval.tv_usec = (int)(quantum_micros%1000000);   // following time intervals, microseconds part

    return timer_sig();
}

/**
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be set as RUNNING. There is no need to
 * provide an entry_point or to create a stack for the main thread - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {
    quantum_counter = 1;
    if (quantum_usecs <= 0) {
        std::cerr << THREAD_LIBRARY_ERR<<QUANTUM_POSITIVE_ERR << std::endl;
        return -1;
    }
    if (sigemptyset(&signal_set)||sigaddset(&signal_set, SIGVTALRM)<0) {
        std::cerr << SYSTEM_ERR << BLOCKED_SIG << std::endl;
        return -1;
    }
    quantum_micros = quantum_usecs;
    signal_set = {0};

    sigjmp_buf env;
    if (sigsetjmp(env, 1) != 0) {
        std::cerr << SYSTEM_ERR<<INIT_CALLED_MORE_THAN_ONCE<< std::endl;
        return -1;
    }
    auto* main_thread = new thread({READY,
                                    1,
                                    0, *env,
                                    true, nullptr});

    running_id = 0;
    max_id_used = 0;
    all_threads[running_id] = main_thread;
    if (make_timer() == -1) {
        return -1;
    }
    return 0;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(thread_entry_point entry_point) {

    block_signal();
    if (max_id_used == MAX_THREAD_NUM-1 and ids_free_from_terminate.empty()) {
        std::cerr << THREAD_LIBRARY_ERR<< REACHED_MAX_THREADS_ERR<<std::endl;
        unblock_signal();
        return -1;
    }
    if (entry_point==NULL) {
        std::cerr << THREAD_LIBRARY_ERR<< ENTRY_POINT_NULL <<std::endl;
        unblock_signal();
        return -1;
    }
    sigjmp_buf env;
    char *stack = create_env(entry_point, env);

    auto* new_thread = new thread({READY, 0, 0, *env, true, stack});
    if (new_thread== nullptr) {
        std::cerr<< THREAD_LIBRARY_ERR<< ALLOC_ERR<<std::endl;
        unblock_signal();
        return -1;
    }
    int next_id = getNextId();
    all_threads[next_id] = new_thread;
    ready_list.push_back(next_id);
    unblock_signal();
    return next_id;
}

/**
 *
 * this function creates the enviroment  with the entry point given for every thread created.
 *
 * @return the stack of the env
*/
char *create_env(thread_entry_point entry_point, __jmp_buf_tag *env) {
    char* stack = new char[STACK_SIZE];
    address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) entry_point;

    sigsetjmp(env, 1);
    (env->__jmpbuf)[JB_SP] = translate_address(sp);
    (env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&env->__saved_mask);
    return stack;
}

/**
 *
 * getter for the next available id. (smallest possible)
 *
 * @return the next id
*/
int getNextId() {
    int next_id;
    if (ids_free_from_terminate.empty()) {
        next_id = max_id_used+1;
        max_id_used++;
    }
    else {
        next_id = ids_free_from_terminate.top();
        ids_free_from_terminate.pop();
    }
    return next_id;
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate(int tid) {
    block_signal();
    if (tid == 0) {
        exit(0);
    }

    if (all_threads.find(tid) == all_threads.end()) {
        std::cerr << THREAD_LIBRARY_ERR<<ID_NOT_EXISTS_ERR << std::endl;
        unblock_signal();
        return -1;
    }
    thread* current_thread = all_threads[tid];
    state thread_status = current_thread->thread_state;
    all_threads.erase(tid);
    delete current_thread;
    ids_free_from_terminate.push(tid);
    if (thread_status == RUNNING) {
        handle_running();
    } else if (thread_status == READY) {
        // remove thread from ready_list
        update_ready_list(tid);
    } else {
        // remove thread from sleep_list
        update_sleep_list(tid);
    }
    unblock_signal();
    return 0;
}

/**
 *
 * This function gets the next ready thread and runs it on the CPU and plaaces the current
 * running in the ready list and changes it's status.
 *
*/
void handle_running() {
    quantum_counter++;
    handle_sleep();
    int next_id = ready_list.front();
    ready_list.pop_front();
    running_id = next_id;
    all_threads[running_id]->thread_state = RUNNING;
    all_threads[next_id]->thread_quantum_running_counter++;
    unblock_signal();
    siglongjmp(all_threads[running_id]->env, 1);
}

/**
 *
 * removes the thread with id tid from the ready_list of threads ids
 *
*/
void update_ready_list(int tid) {
    std::deque<int> new_ready_list;
    for (int thread_id : ready_list) {
        if (thread_id!=tid) {
            new_ready_list.push_back(thread_id);
        }
    }
    ready_list = new_ready_list;
}

/**
 *
 * removes the thread with id tid from the sleep_list of threads ids.
 *
*/
void update_sleep_list(int tid) {
    std::deque<int> new_sleep_list;
    for (int thread_id : sleep_list) {
        if (thread_id!=tid) {
            new_sleep_list.push_back(thread_id);
        }
    }
    sleep_list = new_sleep_list;
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid) {

    block_signal();
    if (all_threads.find(tid) == all_threads.end()) {
        std::cerr << THREAD_LIBRARY_ERR<<ID_NOT_EXISTS_ERR <<std::endl;
        unblock_signal();
        return -1;
    }
    else if (tid == 0) {
        std::cerr << THREAD_LIBRARY_ERR<<BLOCK_MAIN_THREAD_ERR <<std::endl;
        unblock_signal();
        return -1;
    }
    state block_state = all_threads[tid]->thread_state;
    all_threads[tid]->resumed = false;
    all_threads[tid]->thread_state = BLOCKED;
    if (block_state == RUNNING) {
        thread_switch(1);
    }
    else if (block_state == READY) {
        update_ready_list(tid);

    }
    unblock_signal();
    return 0;
}

/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid) {

    block_signal();
    if (all_threads.find(tid) == all_threads.end()) {
        std::cerr << THREAD_LIBRARY_ERR<<ID_NOT_EXISTS_ERR <<std::endl;
        unblock_signal();
        return -1;
    }
    all_threads[tid]->resumed = true;
    if (all_threads[tid]->thread_state == BLOCKED and all_threads[tid]->sleep_time_left <= 0) {
        all_threads[tid]->thread_state = READY;
        ready_list.push_back(tid);
        update_sleep_list(tid);
    }
    unblock_signal();
    return 0;
}

/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid == 0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums) {
    block_signal();

    if (running_id == 0) {
        std::cerr << THREAD_LIBRARY_ERR<<BLOCK_MAIN_THREAD_SLEEP_ERR << std::endl;
        unblock_signal();
        return -1;
    }
    if (num_quantums <= 0) {
        std::cerr << THREAD_LIBRARY_ERR<<NUM_QUANTUM_NEG_ERR<< std::endl;
        unblock_signal();
        return -1;
    }
    quantum_counter++;
    handle_sleep();
    if (sigsetjmp(all_threads[running_id]->env, 1) != 0) {
        unblock_signal();
        return 0;
    }
    all_threads[running_id]->thread_state = BLOCKED;
    all_threads[running_id]->sleep_time_left = num_quantums;
    sleep_list.push_back(running_id);
    int next_id = ready_list.front();
    ready_list.pop_front();
    all_threads[next_id]->thread_state = RUNNING;
    running_id = next_id;
    all_threads[next_id]->thread_quantum_running_counter++;
    unblock_signal();

    siglongjmp(all_threads[running_id]->env, 1);
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid(){
    return running_id;
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums(){
    return (int)quantum_counter;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid){
    block_signal();

    if (all_threads.find(tid)!=all_threads.end()) {
        unblock_signal();

        return all_threads[tid]->thread_quantum_running_counter;
    }

    std::cerr << THREAD_LIBRARY_ERR<< ID_NOT_EXISTS_ERR<<std::endl;
    unblock_signal();
    return -1;
}