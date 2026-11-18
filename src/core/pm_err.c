#include "pm_err.h"

const char *pm_err_str(pm_err_t err)
{
    switch (err) {
    case PM_OK:           return "PM_OK";
    case PM_ERR_PARAM:    return "PM_ERR_PARAM";
    case PM_ERR_NULL:     return "PM_ERR_NULL";
    case PM_ERR_RANGE:    return "PM_ERR_RANGE";
    case PM_ERR_TIMEOUT:  return "PM_ERR_TIMEOUT";
    case PM_ERR_BUSY:     return "PM_ERR_BUSY";
    case PM_ERR_IO:       return "PM_ERR_IO";
    case PM_ERR_STATE:    return "PM_ERR_STATE";
    case PM_ERR_NOMEM:    return "PM_ERR_NOMEM";
    case PM_ERR_AGAIN:    return "PM_ERR_AGAIN";
    case PM_ERR_OVERFLOW: return "PM_ERR_OVERFLOW";
    default:              return "PM_ERR_UNKNOWN";
    }
}
