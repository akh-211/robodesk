#include <sdkconfig.h>

#if defined(CONFIG_APP_ROLLBACK_ENABLE)
// Arduino-ESP32 declares this hook with C linkage in its startup code.
extern "C" bool verifyRollbackLater(void) { return true; }
#endif
