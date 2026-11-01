// Structured diagnostics: every error line is `zaro: error[ZEnnnn]: <message>` on stderr so
// agents can grep by id. Messages that concern a file start with `path:line:` when known.
#pragma once

namespace zaro {

// Stable error ids. Never renumber; add new ids at the end of their group.
enum ErrorId {
    kErrCliUsage = 1,         // ZE0001 bad command line
    kErrPlatformInit = 100,   // ZE0100 SDL / window creation failed
    kErrGpuAdapter = 200,     // ZE0200 no suitable GPU adapter or device
    kErrGpuValidation = 201,  // ZE0201 WebGPU validation or uncaptured error
    kErrShaderLoad = 202,     // ZE0202 shader file missing or unreadable
    kErrGpuReadback = 203,    // ZE0203 buffer map / readback failed
    kErrIo = 300,             // ZE0300 file write failed
};

void log_error(ErrorId id, const char* fmt, ...);
void log_info(const char* fmt, ...);

}  // namespace zaro
