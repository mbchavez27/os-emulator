#include "CommandHandler.h"
#include "Emulator.h"
#include "Scheduler.h"

int main()
{
    // Single owner of app state; handler borrows it by reference, no lifetime issue
    Emulator emulator;
    Scheduler scheduler(Algo::FCFS);
    scheduler.start();

    CommandHandler handler(emulator, scheduler);

    handler.run();
    scheduler.stop();

    return 0;
}