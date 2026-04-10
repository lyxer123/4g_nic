#ifndef SERIAL_CLI_H
#define SERIAL_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start UART interactive CLI (ESP-IDF `console` / linenoise REPL).
 * Call after network / web_service init so mode apply matches web behaviour.
 */
void serial_cli_start(void);

#ifdef __cplusplus
}
#endif

#endif
