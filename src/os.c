#define _POSIX_C_SOURCE 200112L
#include <janet.h>
#include <alloca.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hermes.h"

static void passwd_into_table(struct passwd *pw, JanetTable *t) {
    janet_table_put(t, janet_ckeywordv("name"), janet_cstringv(pw->pw_name));
    janet_table_put(t, janet_ckeywordv("uid"), janet_wrap_number(pw->pw_uid));
    janet_table_put(t, janet_ckeywordv("gid"), janet_wrap_number(pw->pw_gid));
    janet_table_put(t, janet_ckeywordv("gecos"), janet_cstringv(pw->pw_gecos));
    janet_table_put(t, janet_ckeywordv("dir"), janet_cstringv(pw->pw_dir));
    janet_table_put(t, janet_ckeywordv("shell"), janet_cstringv(pw->pw_shell));
}

Janet jgetpwnam(int argc, Janet *argv) {
    JanetTable *info;
    janet_arity(argc, 1, 2);
    struct passwd *pw = getpwnam((const char*)janet_getstring(argv, 0));
    if (argc >= 2) {
        info = janet_gettable(argv, 1);
    } else {
        info = janet_table(8);
    }
    passwd_into_table(pw, info);
    return janet_wrap_table(info);
}

Janet jgetpwuid(int argc, Janet *argv) {
    JanetTable *info;
    janet_arity(argc, 1, 2);
    struct passwd *pw = getpwuid(janet_getinteger(argv, 0));
    if (argc >= 2) {
        info = janet_gettable(argv, 1);
    } else {
        info = janet_table(8);
    }
    passwd_into_table(pw, info);
    return janet_wrap_table(info);
}

Janet jgetgrnam(int argc, Janet *argv) {
    JanetTable *info;
    janet_arity(argc, 1, 2);
    JanetString name = janet_getstring(argv, 0);
    struct group *gr = getgrnam((const char*)name);
    if (!gr)
        janet_panicf("no group named %v found", janet_wrap_string(name));
    if (argc >= 2) {
        info = janet_gettable(argv, 1);
    } else {
        info = janet_table(3);
    }
    janet_table_put(info, janet_ckeywordv("name"), janet_wrap_string(name));
    janet_table_put(info, janet_ckeywordv("gid"), janet_wrap_number(gr->gr_gid));
    JanetArray *memb_array = janet_array(8);
    char **membs = gr->gr_mem;
    while(*membs) {
        janet_array_push(memb_array, janet_cstringv(*(membs++)));
    }
    janet_table_put(info, janet_ckeywordv("members"), janet_wrap_array(memb_array));
    return janet_wrap_table(info);
}

Janet jgetgroups(int argc, Janet *argv) {
    (void)argv;
    janet_fixarity(argc, 0);

    int ngroups = 0;
    gid_t *groups = NULL;

    ngroups = getgroups(ngroups, groups);
    if(ngroups == -1)
        janet_panicf("unable to get group list size - %s", strerror(errno));

    if (ngroups > 1000)
        janet_panicf("user has too many groups");

    groups = alloca(ngroups * sizeof(gid_t));

    if (getgroups(ngroups, groups) == -1)
        janet_panicf("unable to get group list - %s", strerror(errno));

    JanetArray *v = janet_array(ngroups);
    for (int i = 0; i < ngroups; i++) {
        janet_array_push(v, janet_wrap_number(groups[i]));
    }

    return janet_wrap_array(v);
}


Janet jgetuid(int argc, Janet *argv) {
    (void)argv;
    janet_fixarity(argc, 0);
    return janet_wrap_integer(getuid());
}

Janet jgeteuid(int argc, Janet *argv) {
    (void)argv;
    janet_fixarity(argc, 0);
    return janet_wrap_integer(geteuid());
}

Janet jgetgid(int argc, Janet *argv) {
    (void)argv;
    janet_fixarity(argc, 0);
    return janet_wrap_integer(getgid());
}

Janet jgetegid(int argc, Janet *argv) {
    (void)argv;
    janet_fixarity(argc, 0);
    return janet_wrap_integer(getegid());
}

Janet jsetuid(int argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (setuid(janet_getinteger(argv, 0)) != 0)
        janet_panicf("unable to set user id - %s", strerror(errno));
    return janet_wrap_nil();
}

Janet jseteuid(int argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (seteuid(janet_getinteger(argv, 0)) != 0)
        janet_panicf("unable to set effective user id - %s", strerror(errno));
    return janet_wrap_nil();
}

Janet jsetegid(int argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (setegid(janet_getinteger(argv, 0)) != 0)
        janet_panicf("unable to set effective group id - %s", strerror(errno));
    return janet_wrap_nil();
}

static int listen_socket_gc(void *p, size_t len);
static int listen_socket_get(void *p, Janet key, Janet *out);

const JanetAbstractType hermes_listen_socket_type = {
    "_hermes/listen-socket",
    listen_socket_gc,
    NULL,
    listen_socket_get,
    JANET_ATEND_GET
};

static int listen_socket_gc(void *p, size_t len) {
    (void)len;
    int s = *((int*)p);
    if (s < 0)
        close(s);
    return 0;
}

static Janet listen_socket_close(int argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int *ps = janet_getabstract(argv, 0, &hermes_listen_socket_type);
    if (*ps < 0)
        close(*ps);
    *ps = -1;
    return janet_wrap_nil();
}

static Janet listen_socket_accept(int argc, Janet *argv) {

    janet_fixarity(argc, 1);
    int *pls = janet_getabstract(argv, 0, &hermes_listen_socket_type);
    if (*pls < 0)
        janet_panicf("socket closed");

    struct sockaddr_un remote;
    socklen_t sz = sizeof(remote);
    int s = accept(*pls, (struct sockaddr *)&remote, &sz);
    if (s < 0)
        janet_panicf("accept failed - %s", strerror(errno));

    FILE *f = fdopen(s, "w+b");
    if (!f) {
        int _errno = errno;
        close(s);
        janet_panicf("fdopen - %s", strerror(_errno));
    }

    return janet_makefile(f, JANET_FILE_WRITE|JANET_FILE_READ|JANET_FILE_BINARY);
}

static JanetMethod listen_socket_methods[] = {
    {"close", listen_socket_close},
    {"accept", listen_socket_accept},
    {NULL, NULL}
};

static int listen_socket_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD))
        return 0;
    return janet_getmethod(janet_unwrap_keyword(key), listen_socket_methods, out);
}

Janet unix_listen(int argc, Janet *argv) {
    const char *socket_path;
    struct sockaddr_un name;
    size_t size;

    janet_fixarity(argc, 1);
    socket_path = (const char *)janet_getstring(argv, 0);

    int *ps = janet_abstract(&hermes_listen_socket_type, sizeof(int));

    *ps = socket(AF_UNIX, SOCK_STREAM, 0);
    if (*ps < 0)
        janet_panicf("unable to create socket - %s", strerror(errno));

    if ((size_t)janet_string_length(socket_path) >= sizeof(name.sun_path))
        janet_panicf("path %s is too long for a unix socket", socket_path);

    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, socket_path, sizeof(name.sun_path));
    name.sun_path[sizeof (name.sun_path) - 1] = '\0';

    size = (offsetof(struct sockaddr_un, sun_path)
            + strlen (name.sun_path));

    if (bind(*ps, (struct sockaddr *) &name, size) < 0)
        janet_panicf("unable to bind socket %s - %s", socket_path, strerror(errno));

    if (listen(*ps, 100) != 0) /* XXX make this an option? */
        janet_panicf("unable to listen on socket %s - %s", socket_path, strerror(errno));

    return janet_wrap_abstract(ps);
}
