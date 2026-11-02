// ipc.cpp — Pipes con nombre para control del motor.
#include "ipc.h"

std::wstring PipeNameFor(const std::wstring& handlerId) {
    return L"\\\\.\\pipe\\WPXCMD_" + handlerId;
}

InterProcessMessageHandler::InterProcessMessageHandler(const std::wstring& handlerId)
    : pipeName_(PipeNameFor(handlerId)) {
    pipe_ = CreateNamedPipeW(pipeName_.c_str(),
                             PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                             1, 4096, 4096, 0, nullptr);
}

InterProcessMessageHandler::~InterProcessMessageHandler() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
    }
}

bool InterProcessMessageHandler::PeekNextRecvMessageCommand() {
    if (pipe_ == INVALID_HANDLE_VALUE || !cb_) {
        return false;
    }
    DWORD avail = 0;
    if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
        return false;
    }
    wchar_t buf[1024] = {};
    DWORD read = 0;
    if (ReadFile(pipe_, buf, sizeof(buf) - sizeof(wchar_t), &read, nullptr) && read > 0) {
        cb_(buf);
        return true;
    }
    return false;
}
