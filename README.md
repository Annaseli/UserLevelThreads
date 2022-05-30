# UserLevelThreads

User Level Threads Library that mimics the kernel levels threads in their abilities. The difference between 
them to kernel level threads us that it runs on user mode and not in kernel mode so they can't excecute privileged 
operations that the OS can.

FILES:
uthreads.cpp -- the implementation of uthreads.h. a library for threads switching.
Makefile -- the makefile for our excercise.

ANSWERS:

Q1:
If we build a simple OS without supporting threads, it would be convinient and usefull to have a user level
threads in order to use many threads even though our OS does not supports more then one thread.
In this case we have the behaviour of time sharing in our program even though the OS doesnt support the time sharing behavior.

Q2:
Disadvantages:
1) As we studied in class, when we have multiple processes we have bigger overhead time
because the OS has to switch between them for running them on the CPU and load different data each time.
2) It requires more memory than using kernel level threads because each process has its own memory block.

Advantages:
1) There is no need to be worry about synchronization between threads since each process uses its own block of
memory while threads use the same memory and can alter it in the same time which will lead to mistakes and overrides.
2)Since different processes use different memory, the data is more secured in each tab since they dont share data with one anothe.

Q3:
When we use the keyboard to type the command in the shell the process of
the shell sends keyboard interrupt for each key to the OS to indicate that command was typed.
The OS sends signal to the shell to print the letters.
After the command 'kill pid' is written the OS sends signal using the pid to the Shotwell process to be killed.(SIGTERM).
It gets that and the process ends.

Q4:
Virtual time is defined as the time the CPU requires to complete a job if there were no interrupts.
The real time is the actual world time passed.
We used the virtual time in part 2 in this excersize in order to count the number of quantums passed in each CPU run for threads switching.
We can use real time in order to measure the total time a
program runs (Running time is as known very important to measure the preformance of a program).

Q5:
sigsetjump - used for saving the enviroment information of the current thread in order to continue the run of this thread later from the current point. We can indicate by the second parameter if we want
also to save the masked sugnals.
siglongjump - used as a complition for the sigsetjump function. The thread gets the enviorment information that saved from the last sigsetjmp function used for the given enviroment.
The program continues exactrly from the point saved by the enviorment. We can check the return value in the sigsetjmp function to check wheather the function returned from the siglongjmp function
or weather it's the regular saving operation.
