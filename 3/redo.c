#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* Standard C includes */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h> /* fprintf(3), snprintf(3) */
#include <stdlib.h>
#include <string.h>  /* strchr(3), strcmp(3) */

/* POSIX includes */
#include <libgen.h> /* basename(3) */
#include <limits.h>
#include <sys/stat.h> /* stat(3) */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <err.h>

#include "config.h"
#include "aux.h"

int main(int argc, char* argv[]) {
    char const* progname = basename(argv[0]);
    char const* parent_target = getenv("REDO_PARENT");
    const bool redo_chatty = getenv("REDO_VERBOSE") ? true : false;

    if (redo_chatty) {
        xsetenv("REDO_DOFILE_TRACE", "1");
        fprintf(stderr, "redo: invoked as %s\n", progname);
        if (parent_target) {
            fprintf(stderr, "redo: parent target is %s\n", parent_target);
        }
    }

    if (streq(progname, "redo")) {
        /*
         * Command line
         */

        if (argc < 2)
            errx(2, "missing target");

        char target[PATH_MAX] = {0};
        xstrncpy(target, argv[1], (size_t)PATH_MAX);

        /*
         * Determine if target already exists
         */

        bool target_exists = true;  /* optimistic */
        struct stat target_stat = {0};
        if (stat(target, &target_stat) < 0) {
            if (errno == ENOENT) {
                target_exists = false;
                errno = 0;
            } else {
                err(-errno, "stat");
            }
        }

        /*
         * Parse out target file basename and extension
         */

        char* targetc = strdupa(target); /* avoid clobbering original buf */
        char* basep = strrchr(targetc, '/');
        char* targetbase = basep ? basep + 1 : targetc;
        char* extp = strrchr(targetbase, '.');
        char* targetext = 0;
        if (extp) {
            targetext = strdupa(extp);
            targetbase[extp - targetbase] = '\0';
        }

        /*
         * Resolve dofile
         */

        int candno = 1; /* alternative dofile paths tried in order */
        char dofile[PATH_MAX] = {0}; /* current dofile candidate path */
        struct stat dofile_stat = {0}; /* stat info for candidate dofile */

        while (1) {
            switch (candno) {
            case 1:
                xsnprintf(dofile, (size_t)PATH_MAX, "%s.do", target);
                break;
            case 2:
                xsnprintf(dofile, (size_t)PATH_MAX, "default%s.do", targetext ?: "");
                break;
            case 3:
                strcpy(dofile, "default.do");
                break;
            default:
                errx(1, "no dofile");
            }

            if (stat(dofile, &dofile_stat) < 0) {
                if (errno == ENOENT) {
                    errno = 0;
                    ++candno; /* try next candidate */
                    continue;
                } else {
                    err(-errno, "stat");
                }
            } else if ((dofile_stat.st_mode & S_IEXEC) == 0) {
                errx(1, "dofile exists but is not executable: %s", dofile);
            } else {
                break; /* found it */
            }
        }

        if (redo_chatty) {
            fprintf(stderr, "redo: resolved dofile: %s\n", dofile);
        }

        /*
         * Prepare dofile output capture file
         *
         * The temporary outfile is stored in the same folder
         * as the target file, to ensure that atomic rename
         * will work (avoiding cross-dev issues ...)
         */

        char outfile_path[PATH_MAX] = {0};
        xsnprintf(outfile_path, (size_t)PATH_MAX, "%s.tmp.XXXXXX", target);
        int outfd = mkstemp(outfile_path);
        if (outfd < 0)
            err(-errno, "mkstemp");

        /*
         * Execute dofile
         *
         * The dofile is executed directly with parameters
         *   $PATH
         *   $REDO_PARENT
         *   $1 - target file
         *   $2 - target basename
         *   $3 - temporary output
         *
         * Like apenwarr redo, there is no implicit output redirection;
         * dofiles must `exec 1>$3` explicitly to capture build output.
         *
         * No attempt is made to guess the interpreter for the dofile; it must
         * be marked executable and contain the appropriate shebang.
         *
         * The dofile is expected to treat the paths named at $1 and $2 as
         * immutable; the dofile can only write to $3 or stdout/err.
         */

        pid_t dofile_pid = fork();
        switch (dofile_pid) {
        case 0:
            xsetenv("PATH", REDO_EXEC_PREFIX ":" REDO_PATH);
            xsetenv("REDO_PARENT", target);
            execl(dofile, dofile, target, targetbase, outfile_path, (char*)0);
            err(-errno, "exec dofile: %s", dofile);
        case -1:
            err(-errno, "fork");
        }

        /*
         * Await dofile result
         */

        int dofile_status = 0;
        if (waitpid(dofile_pid, &dofile_status, 0) < 0)
            err(-errno, "waitpid");

        if (WIFEXITED(dofile_status)) {
            int ecode = WEXITSTATUS(dofile_status);
            if (ecode == 0) { /* dofile success */
                fsync(outfd);

                struct stat outfile_stat = {0};
                if (fstat(outfd, &outfile_stat) < 0)
                    err(-errno, "fstat");
                if (close(outfd) < 0)
                    err(-errno, "close");

                if (outfile_stat.st_size == 0) { /* phony */
                    if (remove(outfile_path) < 0)
                        err(-errno, "remove");
                } else {
                    if (rename(outfile_path, target) < 0)
                        err(-errno, "rename");
                }
            } else { /* failure */
                remove(outfile_path);
                exit(1);
            }
        } else {
            remove(outfile_path);
            exit(1);
        }
    } else if (streq(progname, "redo-ifchange")) {
        if (!parent_target)
            errx(1, "no REDO_PARENT");
        if (argc < 2)
            errx(2, "missing target");

        char** targets = argv + 1;
        size_t const targetscnt = (size_t)argc - 1;

        /*
         * Resolve prereq file
         */

        char prereqfile_path[PATH_MAX] = {0};
        xsnprintf(prereqfile_path, (size_t)PATH_MAX, "%s.prereq", parent_target);

        /*
         * Record dependency(parent, target) for each target
         *
         * If any of the targets change, the parent target is marked as outdated.
         */

        FILE* prereqh = fopen(prereqfile_path, "a+");
        if (!prereqh)
            err(1, "fopen");

        for (size_t i = 0; i < targetscnt; ++i) {
            char const* target = targets[i];
            size_t const targetlen = strlen(target);

            // XXX: linear scan over prereqs for each target
            rewind(prereqh); // XXX: handle failure?
            bool found = false;
            char linebuf[LINE_MAX] = {0};
            while (!found && fgets(linebuf, LINE_MAX, prereqh)) // XXX: use getdelim?
                found = strncmp(linebuf, target, targetlen) == 0;

            // Determine if search failed due to error
            if (!found && !feof(prereqh))
                err(1, "I/O error");

            // Record new depend
            if (!found)
                fprintf(prereqh, "%s\n", target);
        }

        fsync(fileno(prereqh));
        fclose(prereqh);

        /*
         * Identify & rebuild outdated targets
         *
         * For any outdated target foo, run `redo foo`
         */

        // TODO

        exit(0);
    } else {
        fprintf(stderr, "Usage error: unrecognized program name: %s\n", progname);
        exit(2);
    }
}
