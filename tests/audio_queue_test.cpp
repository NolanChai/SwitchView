// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nolan Chai

#include "../src/audio.h"
#include <iostream>

int main() {
    auto require = [](bool okay, const char* message) { if (!okay) { std::cerr << message << '\n'; exit(1); } };
    AudioQueue queue(sizeof(int16_t), 4);
    const int16_t initial[] = {1, 2, 3}; int16_t output[8]{};
    queue.Push(reinterpret_cast<const BYTE*>(initial), 3, false);
    require(queue.Pop(reinterpret_cast<BYTE*>(output), 2) == 2 && output[0] == 1 && output[1] == 2,
        "Queue must preserve frame order");
    const int16_t overflow[] = {4, 5, 6, 7, 8, 9};
    queue.Push(reinterpret_cast<const BYTE*>(overflow), 6, false);
    require(queue.Size() == 4, "A stalled renderer must not grow the queue");
    require(queue.Pop(reinterpret_cast<BYTE*>(output), 3, 2) == 2 && output[0] == 6 && output[1] == 7 && output[2] == 0,
        "Delay reserve must retain the newest frames and zero-fill an underrun");
    queue.Push(nullptr, 2, true);
    queue.Pop(reinterpret_cast<BYTE*>(output), 4);
    require(output[0] == 8 && output[1] == 9 && output[2] == 0 && output[3] == 0, "Silent packets must be zeroed across wraparound");
    queue.Push(reinterpret_cast<const BYTE*>(initial), 3, false);
    queue.KeepNewest(1);
    require(queue.Pop(reinterpret_cast<BYTE*>(output), 2) == 1 && output[0] == 3 && output[1] == 0,
        "Discarding stale audio must retain the latest frame");
    std::cout << "Audio queue tests passed: ordering, overflow, wraparound, silence, delay reserve, underrun.\n";
}
