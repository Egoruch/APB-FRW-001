#include "cli/CliEngine.h"
#include <stdarg.h>
#include <stdio.h>

void Output::printf(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    print(String(buf));
}

String f2str(float v, int decimals) {
    char buf[24];
    if (decimals <= 0) snprintf(buf, sizeof(buf), "%.0f", v);
    else if (decimals == 1) snprintf(buf, sizeof(buf), "%.1f", v);
    else if (decimals == 2) snprintf(buf, sizeof(buf), "%.2f", v);
    else if (decimals == 3) snprintf(buf, sizeof(buf), "%.3f", v);
    else snprintf(buf, sizeof(buf), "%.2f", v);
    return String(buf);
}

std::vector<String> tokenize(const String &line) {
    std::vector<String> out;
    String cur;
    bool inQuote = false;
    for (unsigned i = 0; i < line.length(); i++) {
        char c = line[i];
        if (c == '"') { inQuote = !inQuote; continue; }
        if (c == ' ' && !inQuote) {
            if (cur.length() > 0) { out.push_back(cur); cur = ""; }
            continue;
        }
        cur += c;
    }
    if (cur.length() > 0) out.push_back(cur);
    return out;
}

int CliEngine::findIndex(const String &name) const {
    for (size_t i = 0; i < m_cmds.size(); i++)
        if (name.equalsIgnoreCase(m_cmds[i].name)) return (int)i;
    return -1;
}

void CliEngine::registerCommand(const char *name, const char *desc, const char *usage,
                                void (*handler)(Output &, const std::vector<String> &)) {
    if (findIndex(name) >= 0) return;   /* avoid duplicates */
    CliCommand c;
    c.name = name;
    c.desc = desc;
    c.usage = usage;
    c.handler = handler;
    m_cmds.push_back(c);
}

bool CliEngine::dispatch(const String &line, Output &out) {
    if (line.length() == 0) return true;
    std::vector<String> args = tokenize(line);
    if (args.empty()) return true;
    String cmd = args[0];
    cmd.toLowerCase();

    if (cmd == "help") {
        if (args.size() >= 2) help(out, args[1]);
        else help(out);
        return true;
    }

    int idx = findIndex(cmd);
    if (idx < 0) {
        out.println("ERROR unknown command: " + cmd);
        return false;
    }
    m_cmds[idx].handler(out, args);
    return true;
}

void CliEngine::help(Output &out) {
    out.println("Available commands:");
    for (size_t i = 0; i < m_cmds.size(); i++) {
        String name(m_cmds[i].name);
        name.trim();
        char line[128];
        snprintf(line, sizeof(line), "  %-14s %s", m_cmds[i].name, m_cmds[i].desc);
        out.println(line);
    }
    out.println("  help <command>   Show help for a command");
}

void CliEngine::help(Output &out, const String &cmd) {
    int idx = findIndex(cmd);
    if (idx < 0) {
        out.println("ERROR no such command: " + cmd);
        return;
    }
    out.println("Command: " + String(m_cmds[idx].name));
    out.println("  " + String(m_cmds[idx].desc));
    if (m_cmds[idx].usage && strlen(m_cmds[idx].usage) > 0)
        out.println("  usage: " + String(m_cmds[idx].usage));
}
