#pragma once
#include <Arduino.h>
#include <vector>

/* Output abstraction so the same CLI engine serves both the hardware UART and
 * the BLE UART, with output routed back to the interface the command came from. */
class Output {
public:
    virtual void print(const String &s) = 0;
    virtual void println(const String &s) = 0;
    virtual void println() = 0;
    virtual void flush() = 0;
    /* line-oriented input */
    virtual int  available() = 0;
    virtual int  read() = 0;

    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

/* Convenience helper used by command handlers for structured output helpers */
String f2str(float v, int decimals = 2);

struct CliCommand {
    const char *name;
    const char *desc;
    const char *usage;
    void (*handler)(Output &out, const std::vector<String> &args);
};

class CliEngine {
public:
    CliEngine() {}

    void registerCommand(const char *name, const char *desc, const char *usage,
                         void (*handler)(Output &, const std::vector<String> &));

    /* Process one non-empty, trimmed line. */
    bool dispatch(const String &line, Output &out);

    void help(Output &out);
    void help(Output &out, const String &cmd);

    size_t commandCount() const { return m_cmds.size(); }

private:
    std::vector<CliCommand> m_cmds;
    int findIndex(const String &name) const;
};

/* Tokenize a line into arguments (space separated). */
std::vector<String> tokenize(const String &line);
