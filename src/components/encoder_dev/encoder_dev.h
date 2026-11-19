/*
 * encoder_dev.h - command interface for the PA8/PA9 quadrature encoder.
 */
#ifndef ENCODER_DEV_H
#define ENCODER_DEV_H

void encoder_dev_init(void);

/* Handles lines starting with "ENC"; returns NULL for anything else.
 * The returned string is a complete reply including CRLF. */
const char *encoder_dev_cmd(const char *line);

#endif /* ENCODER_DEV_H */
