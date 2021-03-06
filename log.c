/*
    This file is part of udpxd.

    Copyright (C) 2015-2016 T.v.Dein.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    You can contact me by mail: <tom AT vondein DOT org>.
*/

#include "log.h"



void verbose(const char * fmt, ...) {
  if(VERBOSE) {
    char *msg = NULL;
    va_list ap;
    va_start(ap, fmt);

    if(vasprintf(&msg, fmt, ap) >= 0) {

      if(FORKED) {
        syslog(LOG_INFO, "%s", msg);
      }
      else {
        fprintf(stderr, "%s", msg);
      }
      free(msg);
      va_end(ap);
    }
    else {
      fprintf(stderr, "Fatal: could not store log message!\n");
      exit(1);
    }
  }
}


void Log(const char * fmt, ...) {
    char *msg = NULL;
    va_list ap;
    va_start(ap, fmt);

    if(vasprintf(&msg, fmt, ap) >= 0) {

      if(FORKED) {
        syslog(LOG_INFO, "%s", msg);
      }
      else {
        fprintf(stderr, "%s", msg);
      }
      free(msg);
      va_end(ap);
    }
    else {
      fprintf(stderr, "Fatal: could not store log message!\n");
      exit(1);
    }
}
