// On Windows, a failed debug-CRT assertion, abort() or a crash opens a message box and
// the test waits for a click forever. Send those reports to stderr instead, so that a
// broken test fails with its message.
#if defined(_WIN32)

#include <crtdbg.h>
#include <windows.h>

#include <cstdlib>
#include <initializer_list>

namespace {

const bool kNoDialogs = [] {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
  }
  return true;
}();

}  // namespace

#endif
