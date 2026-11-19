/*
 * track_dev.h - command interface for the PB14 line-tracking sensor.
 */
#ifndef TRACK_DEV_H
#define TRACK_DEV_H

void track_dev_init(void);

/* Handles lines starting with "TRACK"; returns NULL for anything else.
 * The returned string is a complete reply including CRLF. */
const char *track_dev_cmd(const char *line);

#endif /* TRACK_DEV_H */
