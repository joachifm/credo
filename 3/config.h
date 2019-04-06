#pragma once

/* Command search PATH for dofiles executed by redo */
#define REDO_PATH "/usr/bin"

/* Where to find redo executables, e.g., /usr/lib/redo */
#ifndef REDO_EXEC_PREFIX
#ifdef BUILD_MODE_DEVEL
#define REDO_EXEC_PREFIX "."
#else
#error "Please specify REDO_EXEC_PREFIX"
#endif
#endif
