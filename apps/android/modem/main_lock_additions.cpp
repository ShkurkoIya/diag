/*
 * main_lock_additions.cpp
 *
 * HOW TO INTEGRATE into your existing main.cpp:
 *
 * 1. Add #include "diag_cell_lock.h" at the top
 *
 * 2. Add a DiagCellLock* to shared state (after DiagDciClient client;):
 *      DiagCellLock* locker = nullptr;
 *
 * 3. Add these CLI options to Options struct:
 *      std::string lock_earfcn_pci;   // "38100:455" format
 *      std::string efs_cmd;           // "ls", "tree", "cat"
 *      std::string efs_path;          // EFS path argument
 *
 * 4. Add to parse_args():
 *      else if (a == "-L" && i+1 < argc) o.lock_earfcn_pci = argv[++i];
 *      else if (a == "--efs" && i+1 < argc) { o.efs_cmd = argv[++i]; if(i+1<argc) o.efs_path = argv[++i]; }
 *
 * 5. After client.start() succeeds, insert the code blocks below.
 *
 * 6. Before client.stop(), insert unlock cleanup.
 */

// ═══════════════════════════════════════════════════════════════════════════
// INSERT AFTER: client.start() succeeds, before main loop
// ═══════════════════════════════════════════════════════════════════════════

// ── Create locker ────────────────────────────────────────────────────
DiagCellLock locker(client.client_id());

// ── EFS command mode ─────────────────────────────────────────────────
if (!opts.efs_cmd.empty()) {
    std::string path = opts.efs_path.empty() ? "/" : opts.efs_path;

    if (opts.efs_cmd == "hello") {
        int ret = locker.efs().hello();
        printf("EFS2 hello: %s\n", ret == 0 ? "OK" : "FAILED");

    } else if (opts.efs_cmd == "ls") {
        std::vector<EfsEntry> entries;
        int ret = locker.efs().listdir(path, entries);
        if (ret < 0) {
            fprintf(stderr, "ERROR: listdir '%s' failed (errno=%d)\n",
                    path.c_str(), locker.efs().last_errno());
        } else {
            printf("%-6s %-8s %8s  %s\n", "TYPE", "MODE", "SIZE", "NAME");
            printf("%s\n", std::string(50, '-').c_str());
            for (const auto &e: entries) {
                printf("%-6s %-8s %8u  %s%s\n",
                       e.type_str().c_str(), e.mode_str().c_str(),
                       e.size, e.name.c_str(), e.is_dir ? "/" : "");
            }
            printf("(%zu entries)\n", entries.size());
        }

    } else if (opts.efs_cmd == "tree") {
        // Recursive listing
        std::function<void(const std::string &, int)> tree_fn;
        tree_fn = [&](const std::string &p, int depth) {
            if (depth > 4) return;
            std::vector<EfsEntry> entries;
            if (locker.efs().listdir(p, entries) < 0) return;
            for (const auto &e: entries) {
                printf("%s%s %s%s\n",
                       std::string(depth * 2, ' ').c_str(),
                       e.is_dir ? "[DIR]" : (e.is_item ? "[ITEM]" : "     "),
                       e.name.c_str(), e.is_dir ? "/" : "");
                if (e.is_dir) {
                    std::string sub = p;
                    if (sub.back() != '/') sub += '/';
                    sub += e.name;
                    tree_fn(sub, depth + 1);
                }
            }
        };
        printf("%s/\n", path.c_str());
        tree_fn(path, 1);

    } else if (opts.efs_cmd == "cat" || opts.efs_cmd == "hexdump") {
        std::vector<uint8_t> data;
        int ret = locker.efs().read_file(path, data);
        if (ret < 0) ret = locker.efs().get_item(path, data);
        if (ret < 0) {
            fprintf(stderr, "ERROR: read '%s' failed (errno=%d)\n",
                    path.c_str(), locker.efs().last_errno());
        } else {
            printf("(%zu bytes)\n", data.size());
            for (size_t i = 0; i < data.size(); i += 16) {
                printf("  %04zx: ", i);
                for (size_t j = 0; j < 16; ++j) {
                    if (i + j < data.size()) printf("%02x ", data[i + j]);
                    else
                        printf("   ");
                }
                printf("|");
                for (size_t j = 0; j < 16 && i + j < data.size(); ++j) {
                    uint8_t c = data[i + j];
                    printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
                }
                printf("|\n");
            }
        }

    } else if (opts.efs_cmd == "stat") {
        Efs2StatResult st = {};
        if (locker.efs().stat(path, st) == 0) {
            printf("mode:  0%06o\n", st.mode);
            printf("size:  %u\n", st.size);
            printf("nlink: %u\n", st.nlink);
        } else {
            fprintf(stderr, "ERROR: stat '%s' failed\n", path.c_str());
        }
    }

    // EFS commands are one-shot — exit after
    client.stop();
    return 0;
}

// ── Cell lock ────────────────────────────────────────────────────────
if (!opts.lock_earfcn_pci.empty()) {
    // Parse "EARFCN:PCI" format
    auto colon = opts.lock_earfcn_pci.find(':');
    if (colon == std::string::npos) {
        fprintf(stderr, "ERROR: -L format is EARFCN:PCI (e.g. -L 38100:455)\n");
        client.stop();
        return 1;
    }
    uint32_t earfcn = static_cast<uint32_t>(atoi(opts.lock_earfcn_pci.c_str()));
    uint16_t pci = static_cast<uint16_t>(atoi(opts.lock_earfcn_pci.c_str() + colon + 1));

    printf("%sLocking to EARFCN=%u PCI=%u...%s\n", BOLD, earfcn, pci, RESET);
    if (locker.lock_lte(earfcn, pci) == 0) {
        printf("%s[OK]%s Cell lock applied. Toggle airplane mode to activate.\n",
               GREEN, RESET);
    } else {
        fprintf(stderr, "%s[FAIL]%s Cell lock failed (errno=%d)\n",
                RED, RESET, locker.efs().last_errno());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// INSERT BEFORE: client.stop() at the end (cleanup)
// ═══════════════════════════════════════════════════════════════════════════

// ── Unlock on exit if we locked ──────────────────────────────────────
if (locker.is_lte_locked()) {
    printf("%sUnlocking cell...%s\n", DIM, RESET);
    locker.unlock_lte();
}

// ═══════════════════════════════════════════════════════════════════════════
// ADD TO print_help():
// ═══════════════════════════════════════════════════════════════════════════
/*
            "  -L <EARFCN:PCI>   Lock to LTE cell (e.g. -L 38100:455)\n"
            "  --efs <cmd> [path] EFS2 commands: hello, ls, tree, cat, stat\n"
*/

// ═══════════════════════════════════════════════════════════════════════════
// RENDER: Show lock status in the screen header (add to render_screen)
// ═══════════════════════════════════════════════════════════════════════════
/*
    if (locker.is_lte_locked()) {
        printf(" %s%sLOCKED%s → EARFCN=%u PCI=%u\n",
               BOLD, RED, RESET, locker.locked_earfcn(), locker.locked_pci());
    }
*/
