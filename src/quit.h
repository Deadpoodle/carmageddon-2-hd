#pragma once
// Alt+F4 and window Close quit the game.

namespace quit {

// Before the game registers its window class. Also starts the hang log: when
// the game stops answering its window for 3 s, its main thread's stack.
void Install();

// The main thread's EIP and the code addresses on its stack, to the log.
void LogMainThread();

// Diagnostic: log the focus-related messages the game's window gets.
void LogFocusMessages();

}  // namespace quit
