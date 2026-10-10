#include "common/ProgressWatchdog.h"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

int wmain() {
    const auto file = L"ProgressProbe-" + std::to_wstring(GetCurrentProcessId()) + L".log";
    const auto started = std::chrono::steady_clock::now();
    {
        veil::ProgressWatchdog observer(file);
        observer.Checkpoint("synthetic-blocked-present", 17);
        Sleep(1600); // Deliberate synthetic blocked UI; no GPU/capture capability claim.
        observer.Checkpoint("completed", 18);
    }
    if (std::chrono::steady_clock::now() - started > std::chrono::seconds(3)) { return 1; }
    std::ifstream input{std::filesystem::path(file)};
    std::ostringstream text; text << input.rdbuf();
    if (text.str().find("frame=17,phase=synthetic-blocked-present") == std::string::npos) { return 1; }
    std::cout << "Progress observer PASS: reports blocked phase independently and joins on shutdown; synthetic diagnostic fixture only.\n";
    return 0;
}
