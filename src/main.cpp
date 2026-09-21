#include "CommandHandler.h"
#include "Emulator.h"

int main()
{
    // Instantiate the shared application state
    Emulator emulator;

    // Create the command handler and link it to our emulator instance
    CommandHandler handler(emulator);

    // Enter the main interactive console loop
    handler.run();

    return 0;
}