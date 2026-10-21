/* Future Retro Fusion */
/* src/fsdb_unix.c */
#include "config.h"
#include "sysdeps.h"
#include "fsdb.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

/* Map host errno -> AmigaDOS error codes */
int dos_errno(void)
{
    switch (errno) {
    case ENOMEM:   return ERROR_NO_FREE_STORE;
    case EEXIST:   return ERROR_OBJECT_EXISTS;
    case EACCES:   return ERROR_WRITE_PROTECTED;
    case ENOENT:   return ERROR_OBJECT_NOT_AROUND;
    case ENOTDIR:  return ERROR_OBJECT_WRONG_TYPE;
    case ENOSPC:   return ERROR_DISK_IS_FULL;
    case EBUSY:    return ERROR_OBJECT_IN_USE;
    case EISDIR:   return ERROR_OBJECT_WRONG_TYPE;
#ifdef ETXTBSY
    case ETXTBSY:  return ERROR_OBJECT_IN_USE;
#endif
#ifdef EROFS
    case EROFS:    return ERROR_DISK_WRITE_PROTECTED;
#endif
#ifdef ENOTEMPTY
# if ENOTEMPTY != EEXIST
    case ENOTEMPTY:return ERROR_DIRECTORY_NOT_EMPTY;
# endif
#endif
    default:
        return ERROR_NOT_IMPLEMENTED;
    }
}

/* Reject names that collide with FSDB metadata or . and .. */
int fsdb_name_invalid(const char *n)
{
    if (!n) return 1;
    if (strcmp(n, FSDB_FILE) == 0) return 1;
    if (n[0] != '.') return 0;
    if (n[1] == '\0') return 1;
    return n[1] == '.' && n[2] == '\0';
}

int fsdb_exists(char *nname)
{
    struct stat st;
    fsdb_perf.stats++;
    return stat(nname, &st) != -1;
}

/* Fill attributes from host FS */
int fsdb_fill_file_attrs(a_inode *base, a_inode *aino)
{
    struct stat st;
    if (fsdb_cached_attrs(base, aino))  /* from the directory just read */
        return 1;
    fsdb_perf.stats++;
    if (stat(aino->nname, &st) == -1) return 0;
    aino->dir = S_ISDIR(st.st_mode) ? 1 : 0;
    aino->amigaos_mode =
        ((S_IXUSR & st.st_mode ? 0 : A_FIBF_EXECUTE) |
         (S_IWUSR & st.st_mode ? 0 : A_FIBF_WRITE)   |
         (S_IRUSR & st.st_mode ? 0 : A_FIBF_READ));
    return 1;
}

/* Push AmigaOS protection bits to host FS (owner bits only) */
int fsdb_set_file_attrs(a_inode *aino)
{
    struct stat st;
    if (stat(aino->nname, &st) == -1)
        return ERROR_OBJECT_NOT_AROUND;

    if (!aino->dir) {
        mode_t mode = st.st_mode;
        if (aino->amigaos_mode & A_FIBF_READ)    mode &= ~S_IRUSR; else mode |= S_IRUSR;
        if (aino->amigaos_mode & A_FIBF_WRITE)   mode &= ~S_IWUSR; else mode |= S_IWUSR;
        if (aino->amigaos_mode & A_FIBF_EXECUTE) mode &= ~S_IXUSR; else mode |= S_IXUSR;
        chmod(aino->nname, mode);
    }
    aino->dirty = 1;
    return 0;
}

/* Can we represent these bits on host FS? */
int fsdb_mode_representable_p(const a_inode *aino)
{
    if (aino->dir) return aino->amigaos_mode == 0;
    return (aino->amigaos_mode & (A_FIBF_DELETE|A_FIBF_SCRIPT|A_FIBF_PURE)) == 0;
}

/* Generate unique host name inside base directory */
char *fsdb_create_unique_nname(a_inode *base, const char *suggestion)
{
    char tmp[256] = "__uae___";
    strncat(tmp, suggestion ? suggestion : "", sizeof(tmp) - 1 - strlen(tmp));
    tmp[sizeof(tmp)-1] = 0;

    for (;;) {
        char *p = build_nname(base->nname, tmp);
        if (access(p, R_OK) < 0 && errno == ENOENT)
            return p; /* caller frees */
        free(p);

        /* Make a new random prefix */
        for (int i = 0; i < 8; i++) {
            static const char al[] =
                "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            tmp[i] = al[rand() % (int)(sizeof(al)-1)];
        }
    }
}
