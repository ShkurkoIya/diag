/*
 * efs_browse.cpp — Interactive EFS2 filesystem browser
 *
 * Usage:
 *   ./efs_browse                    # interactive shell
 *   ./efs_browse ls /               # list root
 *   ./efs_browse tree /nv/item_files/  # recursive listing
 *   ./efs_browse cat /nv/item_files/modem/mmode/lte_bandpref
 *   ./efs_browse hexdump /some/path
 *   ./efs_browse get /path > output.bin
 */

#include "diag_dci_client.h"
#include "diag_efs2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <vector>
#include <algorithm>

static volatile bool g_running = true;
static void on_signal(int) { g_running = false; }

// ─────────────────────────────────────────────────────────────────────────────
static void print_hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i += 16) {
        printf("  %04zx: ", i);
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len) printf("%02x ", data[i + j]);
            else printf("   ");
        }
        printf(" |");
        for (size_t j = 0; j < 16 && i + j < len; ++j) {
            uint8_t c = data[i + j];
            printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
        }
        printf("|\n");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
static void cmd_ls(DiagEfs2& efs, const std::string& path) {
    std::vector<EfsEntry> entries;
    int ret = efs.listdir(path, entries);
    if (ret < 0) {
        fprintf(stderr, "  ERROR: listdir failed (errno=%d)\n", efs.last_errno());
        return;
    }

    // Sort: dirs first, then alphabetical
    std::sort(entries.begin(), entries.end(), [](const EfsEntry& a, const EfsEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
    });

    printf("  %-6s %-8s %8s  %s\n", "TYPE", "MODE", "SIZE", "NAME");
    printf("  %s\n", std::string(50, '-').c_str());
    for (const auto& e : entries) {
        printf("  %-6s %-8s %8u  %s%s\n",
               e.type_str().c_str(), e.mode_str().c_str(), e.size,
               e.name.c_str(), e.is_dir ? "/" : "");
    }
    printf("  (%zu entries)\n", entries.size());
}

// ─────────────────────────────────────────────────────────────────────────────
static void cmd_tree(DiagEfs2& efs, const std::string& path, int depth, int max_depth) {
    if (depth > max_depth) return;

    std::vector<EfsEntry> entries;
    int ret = efs.listdir(path, entries);
    if (ret < 0) return;

    std::sort(entries.begin(), entries.end(), [](const EfsEntry& a, const EfsEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
    });

    for (const auto& e : entries) {
        std::string indent(depth * 2, ' ');
        if (e.is_dir) {
            printf("%s  [DIR] %s/\n", indent.c_str(), e.name.c_str());
            std::string sub = path;
            if (sub.back() != '/') sub += '/';
            sub += e.name;
            cmd_tree(efs, sub, depth + 1, max_depth);
        } else {
            printf("%s  %5u %s\n", indent.c_str(), e.size, e.name.c_str());
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
static void cmd_cat(DiagEfs2& efs, const std::string& path) {
    std::vector<uint8_t> data;
    int ret = efs.read_file(path, data);
    if (ret < 0) {
        // Try as item
        ret = efs.get_item(path, data);
    }
    if (ret < 0) {
        fprintf(stderr, "  ERROR: read failed (errno=%d)\n", efs.last_errno());
        return;
    }

    printf("  (%zu bytes)\n", data.size());
    print_hex(data.data(), data.size());
}

// ─────────────────────────────────────────────────────────────────────────────
static void cmd_stat(DiagEfs2& efs, const std::string& path) {
    Efs2StatResult st = {};
    int ret = efs.stat(path, st);
    if (ret < 0) {
        fprintf(stderr, "  ERROR: stat failed (errno=%d)\n", efs.last_errno());
        return;
    }
    printf("  mode : 0%06o\n", st.mode);
    printf("  size : %u\n", st.size);
    printf("  nlink: %u\n", st.nlink);
}

// ─────────────────────────────────────────────────────────────────────────────
static void interactive(DiagEfs2& efs) {
    std::string cwd = "/";
    char line[512];

    printf("\nEFS2 Browser — type 'help' for commands\n\n");

    while (g_running) {
        printf("efs:%s> ", cwd.c_str());
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = 0;
        if (line[0] == 0) continue;

        // Parse command
        std::string cmd = line;
        std::string arg;
        size_t sp = cmd.find(' ');
        if (sp != std::string::npos) {
            arg = cmd.substr(sp + 1);
            cmd = cmd.substr(0, sp);
        }

        // Resolve relative path
        auto resolve = [&](const std::string& p) -> std::string {
            if (p.empty()) return cwd;
            if (p[0] == '/') return p;
            std::string r = cwd;
            if (r.back() != '/') r += '/';
            r += p;
            return r;
        };

        if (cmd == "help" || cmd == "?") {
            printf("  ls [path]        List directory\n");
            printf("  cd <path>        Change directory\n");
            printf("  tree [path] [d]  Recursive listing (d=max depth, default 3)\n");
            printf("  cat <path>       Read & hexdump file/item\n");
            printf("  stat <path>      Show file info\n");
            printf("  hello            Send EFS2 hello\n");
            printf("  quit             Exit\n");
        }
        else if (cmd == "ls") {
            cmd_ls(efs, resolve(arg));
        }
        else if (cmd == "cd") {
            if (arg.empty()) { cwd = "/"; }
            else if (arg[0] == '/') { cwd = arg; }
            else {
                if (cwd.back() != '/') cwd += '/';
                cwd += arg;
            }
            // Normalize
            while (cwd.size() > 1 && cwd.back() == '/') cwd.pop_back();
        }
        else if (cmd == "tree") {
            int max_d = 3;
            std::string tpath = arg;
            sp = arg.find(' ');
            if (sp != std::string::npos) {
                tpath = arg.substr(0, sp);
                max_d = atoi(arg.substr(sp + 1).c_str());
                if (max_d <= 0) max_d = 3;
            }
            printf("  %s/\n", resolve(tpath).c_str());
            cmd_tree(efs, resolve(tpath), 1, max_d);
        }
        else if (cmd == "cat" || cmd == "hexdump" || cmd == "get") {
            if (arg.empty()) { printf("  Usage: cat <path>\n"); continue; }
            cmd_cat(efs, resolve(arg));
        }
        else if (cmd == "stat") {
            if (arg.empty()) { printf("  Usage: stat <path>\n"); continue; }
            cmd_stat(efs, resolve(arg));
        }
        else if (cmd == "hello") {
            int ret = efs.hello();
            printf("  hello: %s\n", ret == 0 ? "OK" : "FAILED");
        }
        else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            break;
        }
        else {
            printf("  Unknown command: %s (type 'help')\n", cmd.c_str());
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // Start DCI client (minimal — no log masks needed, just for command channel)
    DiagDciClient client;
    client.set_log_codes({});  // empty — we don't need any logs

    printf("Starting DIAG DCI for EFS2 access...\n");
    if (!client.start()) {
        fprintf(stderr, "ERROR: Cannot start DCI client\n");
        return 1;
    }
    printf("DCI started (client_id=%d)\n", client.client_id());

    DiagEfs2 efs(client.client_id());

    // Hello handshake
    if (efs.hello() != 0) {
        fprintf(stderr, "WARNING: EFS2 hello failed — commands may not work\n");
    } else {
        printf("EFS2 hello OK\n");
    }

    // Command-line mode or interactive
    if (argc > 1) {
        std::string cmd = argv[1];
        std::string path = argc > 2 ? argv[2] : "/";

        if (cmd == "ls") cmd_ls(efs, path);
        else if (cmd == "tree") {
            int d = argc > 3 ? atoi(argv[3]) : 3;
            printf("  %s/\n", path.c_str());
            cmd_tree(efs, path, 1, d);
        }
        else if (cmd == "cat" || cmd == "hexdump") cmd_cat(efs, path);
        else if (cmd == "stat") cmd_stat(efs, path);
        else { fprintf(stderr, "Unknown command: %s\n", cmd.c_str()); }
    } else {
        interactive(efs);
    }

    client.stop();
    printf("Done.\n");
    return 0;
}
