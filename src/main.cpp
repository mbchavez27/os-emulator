#include "CommandHandler.h"
#include "Emulator.h"

int main()
{
    // Single owner of app state; handler borrows it by reference, no lifetime issue
    Emulator emulator;

    CommandHandler handler(emulator);

    handler.run();

    return 0;
}