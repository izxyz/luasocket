/*=========================================================================*\
* TCP object 
* LuaSocket toolkit
*
* RCS ID: $Id$
\*=========================================================================*/
#include <string.h> 

#include <lua.h>
#include <lauxlib.h>

#include "luasocket.h"

#include "auxiliar.h"
#include "socket.h"
#include "inet.h"
#include "options.h"
#include "tcp.h"

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int global_create(lua_State *L);
static int meth_connect(lua_State *L);
static int meth_bind(lua_State *L);
static int meth_send(lua_State *L);
static int meth_getsockname(lua_State *L);
static int meth_getpeername(lua_State *L);
static int meth_shutdown(lua_State *L);
static int meth_receive(lua_State *L);
static int meth_accept(lua_State *L);
static int meth_close(lua_State *L);
static int meth_setoption(lua_State *L);
static int meth_settimeout(lua_State *L);
static int meth_fd(lua_State *L);
static int meth_dirty(lua_State *L);

/* tcp object methods */
static luaL_reg tcp[] = {
    {"connect",     meth_connect},
    {"send",        meth_send},
    {"receive",     meth_receive},
    {"bind",        meth_bind},
    {"accept",      meth_accept},
    {"setpeername", meth_connect},
    {"setsockname", meth_bind},
    {"getpeername", meth_getpeername},
    {"getsockname", meth_getsockname},
    {"settimeout",  meth_settimeout},
    {"close",       meth_close},
    {"shutdown",    meth_shutdown},
    {"setoption",   meth_setoption},
    {"__gc",        meth_close},
    {"fd",          meth_fd},
    {"dirty",       meth_dirty},
    {NULL,          NULL}
};

/* socket option handlers */
static t_opt opt[] = {
    {"keepalive",   opt_keepalive},
    {"reuseaddr",   opt_reuseaddr},
    {"tcp-nodelay", opt_tcp_nodelay},
    {"linger",      opt_linger},
    {NULL,          NULL}
};

/* functions in library namespace */
static luaL_reg func[] = {
    {"tcp", global_create},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
void tcp_open(lua_State *L)
{
    /* create classes */
    aux_newclass(L, "tcp{master}", tcp);
    aux_newclass(L, "tcp{client}", tcp);
    aux_newclass(L, "tcp{server}", tcp);
    /* create class groups */
    aux_add2group(L, "tcp{master}", "tcp{any}");
    aux_add2group(L, "tcp{client}", "tcp{any}");
    aux_add2group(L, "tcp{server}", "tcp{any}");
    aux_add2group(L, "tcp{client}", "tcp{client,server}");
    aux_add2group(L, "tcp{server}", "tcp{client,server}");
    /* both server and client objects are selectable */
    aux_add2group(L, "tcp{client}", "select{able}");
    aux_add2group(L, "tcp{server}", "select{able}");
    /* define library functions */
    luaL_openlib(L, LUASOCKET_LIBNAME, func, 0); 
    lua_pop(L, 1);
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Just call buffered IO methods
\*-------------------------------------------------------------------------*/
static int meth_send(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkclass(L, "tcp{client}", 1);
    return buf_meth_send(L, &tcp->buf);
}

static int meth_receive(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkclass(L, "tcp{client}", 1);
    return buf_meth_receive(L, &tcp->buf);
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int meth_setoption(lua_State *L)
{
    p_tcp tcp = aux_checkgroup(L, "tcp{any}", 1);
    return opt_meth_setoption(L, opt, &tcp->sock);
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int meth_fd(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkgroup(L, "tcp{client,server}", 1);
    lua_pushnumber(L, tcp->sock);
    return 1;
}

static int meth_dirty(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkgroup(L, "tcp{client,server}", 1);
    lua_pushboolean(L, !buf_isempty(&tcp->buf));
    return 1;
}

/*-------------------------------------------------------------------------*\
* Waits for and returns a client object attempting connection to the 
* server object 
\*-------------------------------------------------------------------------*/
static int meth_accept(lua_State *L)
{
    p_tcp server = (p_tcp) aux_checkclass(L, "tcp{server}", 1);
    p_tm tm = tm_markstart(&server->tm);
    t_sock sock;
    int err = sock_accept(&server->sock, &sock, NULL, NULL, tm);
    /* if successful, push client socket */
    if (err == IO_DONE) {
        p_tcp clnt = lua_newuserdata(L, sizeof(t_tcp));
        aux_setclass(L, "tcp{client}", -1);
        /* initialize structure fields */
        clnt->sock = sock;
        io_init(&clnt->io, (p_send)sock_send, (p_recv)sock_recv, &clnt->sock);
        tm_init(&clnt->tm, -1, -1);
        buf_init(&clnt->buf, &clnt->io, &clnt->tm);
        return 1;
    } else {
        lua_pushnil(L); 
        io_pusherror(L, err);
        return 2;
    }
}

/*-------------------------------------------------------------------------*\
* Turns a master object into a server object
\*-------------------------------------------------------------------------*/
static int meth_bind(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkclass(L, "tcp{master}", 1);
    const char *address =  luaL_checkstring(L, 2);
    unsigned short port = (unsigned short) luaL_checknumber(L, 3);
    int backlog = (int) luaL_optnumber(L, 4, 1);
    const char *err = inet_trybind(&tcp->sock, address, port, backlog);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    /* turn master object into a server object if there was a listen */
    if (backlog >= 0) aux_setclass(L, "tcp{server}", 1);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Turns a master tcp object into a client object.
\*-------------------------------------------------------------------------*/
static int meth_connect(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkclass(L, "tcp{master}", 1);
    const char *address =  luaL_checkstring(L, 2);
    unsigned short port = (unsigned short) luaL_checknumber(L, 3);
    p_tm tm = tm_markstart(&tcp->tm);
    const char *err = inet_tryconnect(&tcp->sock, address, port, tm);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    /* turn master object into a client object */
    aux_setclass(L, "tcp{client}", 1);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object 
\*-------------------------------------------------------------------------*/
static int meth_close(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkgroup(L, "tcp{any}", 1);
    sock_destroy(&tcp->sock);
    return 0;
}

/*-------------------------------------------------------------------------*\
* Shuts the connection down partially
\*-------------------------------------------------------------------------*/
static int meth_shutdown(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkgroup(L, "tcp{any}", 1);
    const char *how = luaL_optstring(L, 2, "both");
    switch (how[0]) {
        case 'b':
            if (strcmp(how, "both")) goto error;
            sock_shutdown(&tcp->sock, 2);
            break;
        case 's':
            if (strcmp(how, "send")) goto error;
            sock_shutdown(&tcp->sock, 1);
            break;
        case 'r':
            if (strcmp(how, "receive")) goto error;
            sock_shutdown(&tcp->sock, 0);
            break;
    }
    return 0;
error:
    luaL_argerror(L, 2, "invalid shutdown method");
    return 0;
}

/*-------------------------------------------------------------------------*\
* Just call inet methods
\*-------------------------------------------------------------------------*/
static int meth_getpeername(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkclass(L, "tcp{client}", 1);
    return inet_meth_getpeername(L, &tcp->sock);
}

static int meth_getsockname(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkgroup(L, "tcp{client,server}", 1);
    return inet_meth_getsockname(L, &tcp->sock);
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int meth_settimeout(lua_State *L)
{
    p_tcp tcp = (p_tcp) aux_checkgroup(L, "tcp{any}", 1);
    return tm_meth_settimeout(L, &tcp->tm);
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master tcp object 
\*-------------------------------------------------------------------------*/
static int global_create(lua_State *L)
{
    t_sock sock;
    const char *err = inet_trycreate(&sock, SOCK_STREAM);
    /* try to allocate a system socket */
    if (!err) { 
        /* allocate tcp object */
        p_tcp tcp = (p_tcp) lua_newuserdata(L, sizeof(t_tcp));
        tcp->sock = sock;
        /* set its type as master object */
        aux_setclass(L, "tcp{master}", -1);
        /* initialize remaining structure fields */
        io_init(&tcp->io, (p_send) sock_send, (p_recv) sock_recv, &tcp->sock);
        tm_init(&tcp->tm, -1, -1);
        buf_init(&tcp->buf, &tcp->io, &tcp->tm);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
}