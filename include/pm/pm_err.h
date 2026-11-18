/*
 * pm_err.h - the single error-code model of the library.
 *
 * Rules (see docs/00-contract.md):
 *  - Every public function returns pm_err_t.
 *  - No module may invent its own error integers.
 *  - PM_OK is 0; all errors are negative, so `if (rc < 0)` is a valid test.
 */
#ifndef PM_ERR_H
#define PM_ERR_H

typedef enum {
    PM_OK           = 0,

    PM_ERR_PARAM    = -1,  /* argument value is invalid (e.g. bad baud rate) */
    PM_ERR_NULL     = -2,  /* required pointer was NULL */
    PM_ERR_RANGE    = -3,  /* index / id outside the configured range */
    PM_ERR_TIMEOUT  = -4,  /* bounded wait expired; partial progress is reported */
    PM_ERR_BUSY     = -5,  /* resource is owned by another caller */
    PM_ERR_IO       = -6,  /* hardware reported an error */
    PM_ERR_STATE    = -7,  /* call is invalid in the current lifecycle state */
    PM_ERR_NOMEM    = -8,  /* a fixed pool / buffer is exhausted */
    PM_ERR_AGAIN    = -9,  /* non-blocking call could not make progress now */
    PM_ERR_OVERFLOW = -10  /* data was dropped because a queue was full */
} pm_err_t;

/* Stable, human-readable name; never returns NULL. */
const char *pm_err_str(pm_err_t err);

#endif /* PM_ERR_H */
