/*
 * relay_dev.h - command interface for the PB5 relay.
 */
#ifndef RELAY_DEV_H
#define RELAY_DEV_H

void relay_dev_init(void);

/* Handles lines starting with "RELAY"; returns NULL for anything else.
 * The returned string is a complete reply including CRLF. */
const char *relay_dev_cmd(const char *line);

#endif /* RELAY_DEV_H */
