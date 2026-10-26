#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <utility>
#include <vector>

namespace fenrir {

class PtyProcess {
public:
    ~PtyProcess() { terminate(); }

    bool spawn(int cols, int rows, std::string& err) {
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
        ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
        pid_ = ::forkpty(&fd_, nullptr, nullptr, &ws);
        if (pid_ < 0) {
            fd_ = -1;
            err = std::strerror(errno);
            return false;
        }
        if (pid_ == 0) { // child: slave is now stdin/stdout/stderr
            // Advertise the Linux virtual-console sequences implemented by
            // Fenrir so utilities such as `clear` emit the right capabilities.
            ::setenv("TERM", "linux", 1);
            const char* sh = ::getenv("SHELL");
            if (!sh || !*sh) sh = "/bin/sh";
            ::execl(sh, sh, static_cast<char*>(nullptr));
            ::_exit(127);
        }
        const int fl = ::fcntl(fd_, F_GETFL, 0);
        if (fl < 0 || ::fcntl(fd_, F_SETFL, fl | O_NONBLOCK) < 0) {
            const int e = errno;
            terminate();
            err = std::strerror(e);
            return false;
        }
        return true;
    }

    int fd() const { return fd_; }
    pid_t pid() const { return pid_; }
    bool hasPendingOutput() const { return !out_.empty(); }

    // Distinguish OSC title changes from the shell prompt versus a foreground
    // child application. PTY output itself does not identify its writer, but
    // the terminal's foreground process group does.
    bool foregroundProcessIsChild() const {
        if (fd_ < 0 || pid_ <= 0) return false;
        const pid_t foreground = ::tcgetpgrp(fd_);
        const pid_t shellGroup = ::getpgid(pid_);
        return foreground > 0 && shellGroup > 0 && foreground != shellGroup;
    }

    void closeFd() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    void forgetChild() { pid_ = -1; }

    void resize(int cols, int rows) {
        if (fd_ < 0) return;
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
        ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
        ::ioctl(fd_, TIOCSWINSZ, &ws); // kernel sends SIGWINCH to the child process group
    }

    // >0: bytes, 0: EAGAIN (no data), -1: EOF/error (slave closed)
    long readSome(char* buf, int cap) {
        ssize_t n;
        do { n = ::read(fd_, buf, static_cast<std::size_t>(cap)); }
        while (n < 0 && errno == EINTR);
        if (n > 0) return static_cast<long>(n);
        if (n == 0) return -1;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }

    void writeBytes(const std::string& s) {
        out_.append(s);
        flushOut();
    }

    void flushOut() {
        while (!out_.empty() && fd_ >= 0) {
            const ssize_t n = ::write(fd_, out_.data(), out_.size());
            if (n > 0) {
                out_.erase(0, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) return;
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            out_.clear(); // other error — discard unwritten bytes
            return;
        }
    }

    void terminate() {
        if (pid_ > 0) {
            ::kill(pid_, SIGHUP);
            int st = 0;
            pid_t r;
            do { r = ::waitpid(pid_, &st, WNOHANG); }
            while (r < 0 && errno == EINTR);
            if (r == 0) {
                ::kill(pid_, SIGKILL);
                do { r = ::waitpid(pid_, &st, 0); }
                while (r < 0 && errno == EINTR);
            }
            pid_ = -1;
        }
        closeFd();
    }

private:
    pid_t pid_ = -1;
    int fd_ = -1;
    std::string out_; // pending output — flushed when writable
};

} // namespace fenrir
