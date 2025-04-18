#pragma once

#ifndef RPAsyncTCP_DEBUG_H
#define RPAsyncTCP_DEBUG_H

#ifdef RPAsyncTCP_DEBUG_PORT
  #define DBG_PORT_ATCP      RPAsyncTCP_DEBUG_PORT
#else
  #define DBG_PORT_ATCP      Serial
#endif

// Change _RPAsyncTCP_LOGLEVEL_ to set tracing and logging verbosity
// 0: DISABLED: no logging
// 1: ERROR: errors
// 2: WARN: errors and warnings
// 3: INFO: errors, warnings and informational (default)
// 4: DEBUG: errors, warnings, informational and debug

#ifndef _RPAsyncTCP_LOGLEVEL_
  #define _RPAsyncTCP_LOGLEVEL_       1
#endif

/////////////////////////////////////////////////////////

#define ATCP_PRINT_MARK      ATCP_PRINT("[ATCP] ")
#define ATCP_PRINT_SP        DBG_PORT_ATCP.print(" ")

#define ATCP_PRINT           DBG_PORT_ATCP.print
#define ATCP_PRINTLN         DBG_PORT_ATCP.println

/////////////////////////////////////////////////////////

#define ATCP_LOGERROR(x)         if(_RPAsyncTCP_LOGLEVEL_>0) { ATCP_PRINT_MARK; ATCP_PRINTLN(x); }
#define ATCP_LOGERROR0(x)        if(_RPAsyncTCP_LOGLEVEL_>0) { ATCP_PRINT(x); }
#define ATCP_LOGERROR1(x,y)      if(_RPAsyncTCP_LOGLEVEL_>0) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINTLN(y); }
#define ATCP_LOGERROR2(x,y,z)    if(_RPAsyncTCP_LOGLEVEL_>0) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINTLN(z); }
#define ATCP_LOGERROR3(x,y,z,w)  if(_RPAsyncTCP_LOGLEVEL_>0) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINT(z); ATCP_PRINT_SP; ATCP_PRINTLN(w); }

/////////////////////////////////////////////////////////

#define ATCP_LOGWARN(x)          if(_RPAsyncTCP_LOGLEVEL_>1) { ATCP_PRINT_MARK; ATCP_PRINTLN(x); }
#define ATCP_LOGWARN0(x)         if(_RPAsyncTCP_LOGLEVEL_>1) { ATCP_PRINT(x); }
#define ATCP_LOGWARN1(x,y)       if(_RPAsyncTCP_LOGLEVEL_>1) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINTLN(y); }
#define ATCP_LOGWARN2(x,y,z)     if(_RPAsyncTCP_LOGLEVEL_>1) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINTLN(z); }
#define ATCP_LOGWARN3(x,y,z,w)   if(_RPAsyncTCP_LOGLEVEL_>1) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINT(z); ATCP_PRINT_SP; ATCP_PRINTLN(w); }

/////////////////////////////////////////////////////////

#define ATCP_LOGINFO(x)          if(_RPAsyncTCP_LOGLEVEL_>2) { ATCP_PRINT_MARK; ATCP_PRINTLN(x); }
#define ATCP_LOGINFO0(x)         if(_RPAsyncTCP_LOGLEVEL_>2) { ATCP_PRINT(x); }
#define ATCP_LOGINFO1(x,y)       if(_RPAsyncTCP_LOGLEVEL_>2) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINTLN(y); }
#define ATCP_LOGINFO2(x,y,z)     if(_RPAsyncTCP_LOGLEVEL_>2) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINTLN(z); }
#define ATCP_LOGINFO3(x,y,z,w)   if(_RPAsyncTCP_LOGLEVEL_>2) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINT(z); ATCP_PRINT_SP; ATCP_PRINTLN(w); }

/////////////////////////////////////////////////////////

#define ATCP_LOGDEBUG(x)         if(_RPAsyncTCP_LOGLEVEL_>3) { ATCP_PRINT_MARK; ATCP_PRINTLN(x); }
#define ATCP_LOGDEBUG0(x)        if(_RPAsyncTCP_LOGLEVEL_>3) { ATCP_PRINT(x); }
#define ATCP_LOGDEBUG1(x,y)      if(_RPAsyncTCP_LOGLEVEL_>3) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINTLN(y); }
#define ATCP_LOGDEBUG2(x,y,z)    if(_RPAsyncTCP_LOGLEVEL_>3) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINTLN(z); }
#define ATCP_LOGDEBUG3(x,y,z,w)  if(_RPAsyncTCP_LOGLEVEL_>3) { ATCP_PRINT_MARK; ATCP_PRINT(x); ATCP_PRINT_SP; ATCP_PRINT(y); ATCP_PRINT_SP; ATCP_PRINT(z); ATCP_PRINT_SP; ATCP_PRINTLN(w); }

/////////////////////////////////////////////////////////

#endif    //RPAsyncTCP_DEBUG_H
