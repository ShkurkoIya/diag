/*
 * diag_efs2.h — EFS2 filesystem client via DIAG DCI
 *
 * References:
 *   - QCSuper: src/qcsuper/protocol/efs2.py
 *   - qtools:  efsio.c (forth32)
 *   - Qualcomm.txt (project docs)
 *
 * Protocol:
 *   All EFS2 commands go through DIAG_SUBSYS_CMD_F (0x4B)
 *   with subsys_id = DIAG_SUBSYS_FS (19 = 0x13)
 *   and subsys_cmd = EFS2_DIAG_* (see enum below)
 *
 *   Request:  [0x4B] [0x13] [cmd_lo] [cmd_hi] [payload...]
 *   Response: [0x4B] [0x13] [cmd_lo] [cmd_hi] [result...]
 */

#pragma once
#ifndef DIAG_EFS2_H
#define DIAG_EFS2_H

#include "libdiag_loader.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// EFS2 DIAG commands (subsys_cmd values)
// ─────────────────────────────────────────────────────────────────────────────
enum Efs2Cmd : uint16_t {
    EFS2_DIAG_HELLO       =  0,
    EFS2_DIAG_QUERY       =  1,
    EFS2_DIAG_OPEN        =  2,
    EFS2_DIAG_CLOSE       =  3,
    EFS2_DIAG_READ        =  4,
    EFS2_DIAG_WRITE       =  5,
    EFS2_DIAG_SYMLINK     =  6,
    EFS2_DIAG_READLINK    =  7,
    EFS2_DIAG_UNLINK      =  8,
    EFS2_DIAG_MKDIR       =  9,
    EFS2_DIAG_RMDIR       = 10,
    EFS2_DIAG_OPENDIR     = 11,
    EFS2_DIAG_READDIR     = 12,
    EFS2_DIAG_CLOSEDIR    = 13,
    EFS2_DIAG_RENAME      = 14,
    EFS2_DIAG_STAT        = 15,
    EFS2_DIAG_LSTAT       = 16,
    EFS2_DIAG_FSTAT       = 17,
    EFS2_DIAG_CHMOD       = 18,
    EFS2_DIAG_STATFS      = 19,
    EFS2_DIAG_ACCESS      = 20,
    EFS2_DIAG_PUT         = 38,
    EFS2_DIAG_GET         = 39,
};

// ─────────────────────────────────────────────────────────────────────────────
// EFS2 structures
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)

struct Efs2SubsysHeader {
    uint8_t  cmd_code;    // 0x4B = DIAG_SUBSYS_CMD_F
    uint8_t  subsys_id;   // 0x13 = DIAG_SUBSYS_FS
    uint16_t subsys_cmd;  // EFS2_DIAG_* (little-endian)
};

struct Efs2StatResult {
    int32_t  diag_errno;
    uint32_t mode;
    uint32_t size;
    uint32_t nlink;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
};

struct Efs2DirentResult {
    uint32_t dirp;
    int32_t  seqno;
    int32_t  diag_errno;
    uint32_t entry_type;
    uint32_t mode;
    uint32_t size;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    char     name[256];   // null-terminated
};

#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// Directory entry for listings
// ─────────────────────────────────────────────────────────────────────────────
struct EfsEntry {
    std::string name;
    uint32_t    mode    = 0;
    uint32_t    size    = 0;
    bool        is_dir  = false;
    bool        is_file = false;
    bool        is_item = false;   // EFS item file (S_IFITM = 0160000)

    std::string type_str() const {
        if (is_dir)  return "DIR";
        if (is_item) return "ITEM";
        if (is_file) return "FILE";
        return "???";
    }

    std::string mode_str() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%06o", mode & 0xFFFF);
        return buf;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// EFS2 Client
// ─────────────────────────────────────────────────────────────────────────────
class DiagEfs2 {
public:
    explicit DiagEfs2(int client_id) : client_id_(client_id) {}

    void set_debug(bool on) { debug_ = on; }

    // ── Directory listing ────────────────────────────────────────────────────
    int listdir(const std::string& path, std::vector<EfsEntry>& out);

    // ── Stat ─────────────────────────────────────────────────────────────────
    int stat(const std::string& path, Efs2StatResult& out);

    // ── Read file contents ───────────────────────────────────────────────────
    int read_file(const std::string& path, std::vector<uint8_t>& out);

    // ── Get item (EFS2_DIAG_GET) ─────────────────────────────────────────────
    int get_item(const std::string& path, std::vector<uint8_t>& out);

    // ── Put item (EFS2_DIAG_PUT) ─────────────────────────────────────────────
    int put_item(const std::string& path, const uint8_t* data, size_t len);

    // ── Write file ───────────────────────────────────────────────────────────
    int write_file(const std::string& path, const uint8_t* data, size_t len);

    // ── Delete ───────────────────────────────────────────────────────────────
    int unlink(const std::string& path);
    int rmdir(const std::string& path);
    int mkdir(const std::string& path, uint16_t mode = 0755);

    // ── Hello/Query ──────────────────────────────────────────────────────────
    int hello();

    // ── Last errno ───────────────────────────────────────────────────────────
    int last_errno() const { return last_errno_; }

private:
    // Send EFS2 command and wait for synchronous response
    int send_efs_cmd(Efs2Cmd cmd, const void* req_payload, size_t req_len,
                     void* rsp_buf, size_t rsp_buf_size);

    // DCI async response handler
    static void dci_response_cb(unsigned char* buf, int len, void* ctx);

    int          client_id_;
    int          last_errno_  = 0;
    bool         debug_       = false;

    // Synchronization for async→sync conversion
    std::mutex               rsp_mutex_;
    std::condition_variable  rsp_cv_;
    bool                     rsp_ready_   = false;
    std::vector<uint8_t>     rsp_data_;
};


// ═════════════════════════════════════════════════════════════════════════════
// Implementation
// ═════════════════════════════════════════════════════════════════════════════

inline void DiagEfs2::dci_response_cb(unsigned char* buf, int len, void* ctx) {
    auto* self = static_cast<DiagEfs2*>(ctx);
    std::lock_guard<std::mutex> lk(self->rsp_mutex_);
    self->rsp_data_.assign(buf, buf + len);
    self->rsp_ready_ = true;
    self->rsp_cv_.notify_one();
}

// Debug hex dump helper
static inline void efs_hex_dump(const char* label, const uint8_t* data, size_t len, size_t max_bytes = 64) {
    fprintf(stderr, "  [EFS2] %s (%zu bytes):", label, len);
    size_t show = (len < max_bytes) ? len : max_bytes;
    for (size_t i = 0; i < show; ++i) {
        if (i % 16 == 0) fprintf(stderr, "\n    %04zx: ", i);
        fprintf(stderr, "%02X ", data[i]);
    }
    if (len > max_bytes) fprintf(stderr, "\n    ... (+%zu)", len - max_bytes);
    fprintf(stderr, "\n");
}

inline int DiagEfs2::send_efs_cmd(Efs2Cmd cmd, const void* req_payload, size_t req_len,
                                   void* rsp_buf, size_t rsp_buf_size) {
    auto& lib = LibdiagLoader::instance();
    if (!lib.send_dci_async_req) {
        if (debug_) fprintf(stderr, "  [EFS2] ERROR: send_dci_async_req not available\n");
        last_errno_ = -1;
        return -1;
    }

    // Build command: header(4) + payload
    std::vector<uint8_t> cmd_buf(4 + req_len);
    cmd_buf[0] = 0x4B;                           // DIAG_SUBSYS_CMD_F
    cmd_buf[1] = 0x13;                           // DIAG_SUBSYS_FS
    cmd_buf[2] = static_cast<uint8_t>(cmd & 0xFF);
    cmd_buf[3] = static_cast<uint8_t>((cmd >> 8) & 0xFF);
    if (req_len > 0 && req_payload) {
        memcpy(cmd_buf.data() + 4, req_payload, req_len);
    }

    if (debug_) {
        fprintf(stderr, "  [EFS2] CMD=%u (0x%02X)\n", cmd, cmd);
        efs_hex_dump("REQ", cmd_buf.data(), cmd_buf.size());
    }

    // Response buffer (also passed to DCI — some impls write here directly)
    uint8_t rsp_raw[4096] = {};

    // Reset sync state
    {
        std::lock_guard<std::mutex> lk(rsp_mutex_);
        rsp_ready_ = false;
        rsp_data_.clear();
    }

    // Send
    int ret = lib.send_dci_async_req(
            client_id_,
            cmd_buf.data(),
            static_cast<int>(cmd_buf.size()),
            rsp_raw,
            sizeof(rsp_raw),
            &DiagEfs2::dci_response_cb,
            this
    );

    if (debug_) fprintf(stderr, "  [EFS2] send_dci_async_req returned %d (OK=%d)\n",
                        ret, DIAG_DCI_NO_ERROR);

    if (ret != DIAG_DCI_NO_ERROR) {
        last_errno_ = ret;
        return -1;
    }

    // Wait for response (timeout 5s)
    {
        std::unique_lock<std::mutex> lk(rsp_mutex_);
        if (!rsp_cv_.wait_for(lk, std::chrono::seconds(5), [this] { return rsp_ready_; })) {
            if (debug_) fprintf(stderr, "  [EFS2] TIMEOUT waiting for response\n");
            last_errno_ = -2;
            return -1;
        }
    }

    if (debug_) {
        efs_hex_dump("RAW RSP (callback)", rsp_data_.data(), rsp_data_.size());
        // Also check if rsp_raw has data (some DCI impls write here)
        bool rsp_raw_has_data = false;
        for (int i = 0; i < 16; ++i) if (rsp_raw[i]) { rsp_raw_has_data = true; break; }
        if (rsp_raw_has_data) {
            efs_hex_dump("RAW RSP (rsp_raw buf)", rsp_raw, 64);
        }
    }

    // ── Find where the actual payload starts ─────────────────────────────
    // The DCI callback may deliver:
    //   A) Full subsys response: [4B 13 cmd_lo cmd_hi] [payload...]
    //   B) Just the payload (no header)
    //   C) With extra DCI framing
    //
    // Detect by checking if first bytes match the subsys header
    const uint8_t* rsp_ptr = rsp_data_.data();
    size_t rsp_len = rsp_data_.size();

    size_t hdr_skip = 0;
    if (rsp_len >= 4 && rsp_ptr[0] == 0x4B && rsp_ptr[1] == 0x13) {
        // Response starts with subsys header — skip it
        hdr_skip = 4;
        if (debug_) fprintf(stderr, "  [EFS2] Detected subsys header, skip=%zu\n", hdr_skip);
    } else {
        // No subsys header — use raw
        hdr_skip = 0;
        if (debug_) fprintf(stderr, "  [EFS2] No subsys header detected, skip=0\n");
    }

    if (rsp_len < hdr_skip) {
        last_errno_ = -3;
        return -1;
    }

    size_t payload_len = rsp_len - hdr_skip;
    size_t copy_len = std::min(payload_len, rsp_buf_size);
    if (copy_len > 0) {
        memcpy(rsp_buf, rsp_ptr + hdr_skip, copy_len);
    }

    if (debug_) {
        fprintf(stderr, "  [EFS2] Payload (%zu bytes after skip %zu):\n", payload_len, hdr_skip);
        efs_hex_dump("PAYLOAD", rsp_ptr + hdr_skip,
                     payload_len < 64 ? payload_len : 64);
    }

    return static_cast<int>(payload_len);
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::hello() {
    struct { uint32_t target_pkt_window; uint32_t version; uint32_t max_dirs; uint32_t max_files; } rsp = {};
    int ret = send_efs_cmd(EFS2_DIAG_HELLO, nullptr, 0, &rsp, sizeof(rsp));
    if (ret < 0) return -1;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::stat(const std::string& path, Efs2StatResult& out) {
    int ret = send_efs_cmd(EFS2_DIAG_STAT, path.c_str(), path.size() + 1, &out, sizeof(out));
    if (ret < 0) return -1;
    last_errno_ = out.diag_errno;
    return out.diag_errno == 0 ? 0 : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::listdir(const std::string& path, std::vector<EfsEntry>& out) {
    out.clear();

    // OPENDIR
    struct { uint32_t dirp; int32_t diag_errno; } open_rsp = {};
    int ret = send_efs_cmd(EFS2_DIAG_OPENDIR, path.c_str(), path.size() + 1,
                           &open_rsp, sizeof(open_rsp));
    if (ret < 0 || open_rsp.diag_errno != 0) {
        last_errno_ = open_rsp.diag_errno;
        return -1;
    }
    uint32_t dirp = open_rsp.dirp;

    // READDIR loop
    for (int seq = 0; seq < 512; ++seq) {
        struct { uint32_t dirp; int32_t seqno; } rd_req = { dirp, seq };
        Efs2DirentResult entry = {};

        ret = send_efs_cmd(EFS2_DIAG_READDIR, &rd_req, sizeof(rd_req),
                           &entry, sizeof(entry));
        if (ret < 0) break;
        if (entry.diag_errno != 0) break;  // end of dir

        // entry_type: 0=file, 1=dir, 2=symlink
        EfsEntry e;
        e.name   = entry.name;
        e.mode   = entry.mode;
        e.size   = entry.size;
        e.is_dir  = ((entry.mode & 0xF000) == 0x4000);  // S_IFDIR
        e.is_file = ((entry.mode & 0xF000) == 0x8000);  // S_IFREG
        e.is_item = ((entry.mode & 0xF000) == 0xE000);  // S_IFITM

        if (e.name != "." && e.name != "..") {
            out.push_back(e);
        }
    }

    // CLOSEDIR
    struct { int32_t diag_errno; } close_rsp = {};
    send_efs_cmd(EFS2_DIAG_CLOSEDIR, &dirp, 4, &close_rsp, sizeof(close_rsp));

    return static_cast<int>(out.size());
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::read_file(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();

    // OPEN (O_RDONLY = 0)
    struct { int32_t oflag; int32_t mode; char name[256]; } open_req = {};
    open_req.oflag = 0;  // O_RDONLY
    open_req.mode  = 0;
    strncpy(open_req.name, path.c_str(), sizeof(open_req.name) - 1);

    struct { int32_t fd; int32_t diag_errno; } open_rsp = {};
    int ret = send_efs_cmd(EFS2_DIAG_OPEN, &open_req, 8 + path.size() + 1,
                           &open_rsp, sizeof(open_rsp));
    if (ret < 0 || open_rsp.fd < 0) {
        last_errno_ = open_rsp.diag_errno;
        return -1;
    }
    int fd = open_rsp.fd;

    // READ loop
    uint32_t offset = 0;
    while (offset < 1024 * 1024) {  // 1MB limit
        struct { int32_t fd; uint32_t nbyte; uint32_t offset; } rd_req = {
                fd, 2048, offset
        };
        struct {
            int32_t  fd;
            uint32_t offset;
            int32_t  bytes_read;
            int32_t  diag_errno;
            uint8_t  data[2048];
        } rd_rsp = {};

        ret = send_efs_cmd(EFS2_DIAG_READ, &rd_req, sizeof(rd_req),
                           &rd_rsp, sizeof(rd_rsp));
        if (ret < 0 || rd_rsp.bytes_read <= 0) break;

        out.insert(out.end(), rd_rsp.data, rd_rsp.data + rd_rsp.bytes_read);
        offset += rd_rsp.bytes_read;

        if (rd_rsp.bytes_read < 2048) break;  // EOF
    }

    // CLOSE
    struct { int32_t diag_errno; } close_rsp = {};
    send_efs_cmd(EFS2_DIAG_CLOSE, &fd, 4, &close_rsp, sizeof(close_rsp));

    return static_cast<int>(out.size());
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::get_item(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();

    // GET: path (null-terminated)
    uint8_t rsp_buf[4096] = {};
    int ret = send_efs_cmd(EFS2_DIAG_GET, path.c_str(), path.size() + 1,
                           rsp_buf, sizeof(rsp_buf));
    if (ret < 0) return -1;

    // Response: diag_errno(4) + data
    if (ret < 4) return -1;
    int32_t err;
    memcpy(&err, rsp_buf, 4);
    if (err != 0) { last_errno_ = err; return -1; }

    out.assign(rsp_buf + 4, rsp_buf + ret);
    return static_cast<int>(out.size());
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::put_item(const std::string& path, const uint8_t* data, size_t len) {
    // PUT per Qualcomm.txt: data_size(4) + flags(4) + path + \0 + value
    // But simpler API: EFS2_DIAG_PUT = data + \0 + path + \0
    std::vector<uint8_t> req;

    // QCSuper format: path\0 + data
    // qtools format: data first, then path
    // Standard EFS2 PUT: oflag(4) + mode(4) + data(N) + path\0

    // Use open/write/close for regular files instead
    // For item files, the format is:
    //   path\0 + data
    size_t total = path.size() + 1 + len;
    req.resize(total);
    memcpy(req.data(), path.c_str(), path.size() + 1);
    if (len > 0) memcpy(req.data() + path.size() + 1, data, len);

    uint8_t rsp[16] = {};
    int ret = send_efs_cmd(EFS2_DIAG_PUT, req.data(), req.size(), rsp, sizeof(rsp));
    if (ret < 4) return -1;

    int32_t err;
    memcpy(&err, rsp, 4);
    last_errno_ = err;
    return err == 0 ? 0 : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::write_file(const std::string& path, const uint8_t* data, size_t len) {
    // OPEN (O_WRONLY | O_CREAT | O_TRUNC = 0x301)
    struct { int32_t oflag; int32_t mode; char name[256]; } open_req = {};
    open_req.oflag = 0x301;  // O_WRONLY | O_CREAT | O_TRUNC
    open_req.mode  = 0666;
    strncpy(open_req.name, path.c_str(), sizeof(open_req.name) - 1);

    struct { int32_t fd; int32_t diag_errno; } open_rsp = {};
    int ret = send_efs_cmd(EFS2_DIAG_OPEN, &open_req, 8 + path.size() + 1,
                           &open_rsp, sizeof(open_rsp));
    if (ret < 0 || open_rsp.fd < 0) {
        last_errno_ = open_rsp.diag_errno;
        return -1;
    }
    int fd = open_rsp.fd;

    // WRITE
    uint32_t offset = 0;
    while (offset < len) {
        size_t chunk = std::min(len - offset, (size_t)2048);
        std::vector<uint8_t> wr_req(8 + chunk);
        memcpy(wr_req.data(), &fd, 4);
        memcpy(wr_req.data() + 4, &offset, 4);
        memcpy(wr_req.data() + 8, data + offset, chunk);

        struct { int32_t fd; uint32_t offset; int32_t written; int32_t diag_errno; } wr_rsp = {};
        ret = send_efs_cmd(EFS2_DIAG_WRITE, wr_req.data(), wr_req.size(),
                           &wr_rsp, sizeof(wr_rsp));
        if (ret < 0 || wr_rsp.written <= 0) break;
        offset += wr_rsp.written;
    }

    // CLOSE
    struct { int32_t diag_errno; } close_rsp = {};
    send_efs_cmd(EFS2_DIAG_CLOSE, &fd, 4, &close_rsp, sizeof(close_rsp));

    return static_cast<int>(offset);
}

// ─────────────────────────────────────────────────────────────────────────────
inline int DiagEfs2::unlink(const std::string& path) {
    int32_t err = 0;
    send_efs_cmd(EFS2_DIAG_UNLINK, path.c_str(), path.size() + 1, &err, sizeof(err));
    last_errno_ = err;
    return err == 0 ? 0 : -1;
}

inline int DiagEfs2::rmdir(const std::string& path) {
    int32_t err = 0;
    send_efs_cmd(EFS2_DIAG_RMDIR, path.c_str(), path.size() + 1, &err, sizeof(err));
    last_errno_ = err;
    return err == 0 ? 0 : -1;
}

inline int DiagEfs2::mkdir(const std::string& path, uint16_t mode) {
    struct { int16_t mode; char name[256]; } req = {};
    req.mode = static_cast<int16_t>(mode);
    strncpy(req.name, path.c_str(), sizeof(req.name) - 1);

    int32_t err = 0;
    send_efs_cmd(EFS2_DIAG_MKDIR, &req, 2 + path.size() + 1, &err, sizeof(err));
    last_errno_ = err;
    return err == 0 ? 0 : -1;
}

#endif // DIAG_EFS2_H
