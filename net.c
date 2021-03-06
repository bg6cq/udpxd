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

#include "net.h"
#include "client.h"
#include "host.h"
#include "log.h"

/* bind to a socket, either for listen() or for outgoing src ip binding */
int bindsocket( host_t *sock_h) {
  int fd;
  int err = 0;

  if(sock_h->ss.ss_family == AF_INET6) {
    fd = socket( PF_INET6, SOCK_DGRAM, IPPROTO_UDP );
  }
  else {
    fd = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP );
  }

  if( ! ( fd >= 0 && -1 != bind( fd, (struct sockaddr*)&sock_h->ss, sizeof(struct sockaddr_storage) ) ) ) {
    err = 1;
  }

  if(err) {
    fprintf( stderr, "Cannot bind address ([%s]:%d)\n", host_ip(sock_h), host_port(sock_h));
    perror(NULL);
    return -1;
  }
  
  return fd;
}

/*
  returns:
 -1: error in any case
  0: parent not forked (fork disabled)
  1: parent after successful fork
  2: child after successful fork
 */
int daemonize(char *pidfile) {
  if(FORKED) {
    // fork
    pid_t pid, sid;
    FILE *fd;

    pid = fork();

    if (pid < 0) {
      perror("fork error");
      return 1;
    }

    if (pid > 0) {
      /* leave parent */
      if((fd = fopen(pidfile, "w")) == NULL) {
        perror("failed to write pidfile");
        return -1;
      }
      else {
        fprintf(fd, "%d\n", pid);
        fclose(fd);
      }
      return 1;
    }

    /* child */

    sid = setsid();
    if (sid < 0) {
      perror("set sid error");
      return 1;
    }
  
    umask(0);
 
    openlog("udpxd", LOG_NOWAIT|LOG_PID, LOG_USER);

    return 2;
  }

  return 0;
}

int drop_privileges(char *user, char *chrootdir) {
  struct passwd *pw = getpwnam(user);
  uid_t me = getuid();
    
  if(!FORKED)
    return 0;

  if ((chdir("/")) < 0) {
    perror("failed to chdir to /");
    return 1;
  }

  if(me == 0) {
    /* drop privileges */
    if(chroot(chrootdir) != 0) {
      perror("failed to chroot");
      return 1;
    }
    
    if(pw == NULL) {
      perror("user not found");
      fprintf(stderr,"user name: %s\n",user);
      return 1;
    }

    if(setegid(pw->pw_gid) != 0) {
      perror("could not set egid");
      return 1;
    }

    if(setgid(pw->pw_gid) != 0) {
      perror("could not set gid");
      return 1;
    }

    if(setuid(pw->pw_uid) != 0) {
      perror("could not set uid");
      return 1;
    }

    if(seteuid(pw->pw_uid) != 0) {
      perror("could not set euid");
      return 1;
    }

    if (setuid(0) != -1) {
      fprintf(stderr, "error, managed to regain root privileges after dropping them!\n");
      return 1;
    }
  }

  return 0;
}

int start_listener (char *inip, char *inpt, char *srcip, char *srcpt, char *dstip,
                    char *dstpt, char *pidfile, char *chrootdir, char *user) {
  host_t *listen_h, *dst_h, *bind_h;

  int dm = daemonize(pidfile);
  switch(dm) {
  case -1:
    return 1; /* parent, fork error */
    break;
  case 0:
    break;    /* parent, not forking */
  case 1:
    return 0; /* parent, fork ok, leave */
    break;
  case 2:
    break;    /* child, fork ok, continue */
  }
  
  listen_h = get_host(inip, atoi(inpt), NULL);
  dst_h    = get_host(dstip, atoi(dstpt), NULL);
  bind_h   = NULL;

  if(srcip != NULL) {
    bind_h   = get_host(srcip, atoi(srcpt), NULL);
  }
  else {
    if(dst_h->ss.ss_family == AF_INET6 )
      bind_h = get_host("::0", 0, NULL);
    else
      bind_h = get_host("0.0.0.0", 0, NULL);
  }

  int listen = bindsocket(listen_h);

  if(listen == -1)
    return 1;

  if(VERBOSE) {
    verbose("Listening on %s:%s, forwarding to %s:%s",
            host_ip(listen_h), inpt, host_ip2(dst_h), dstpt);
    if(srcip != NULL)
      verbose(", binding to %s\n", host_ip(bind_h));
    else
      verbose("\n");
  }

  if(drop_privileges(user, chrootdir) != 0) {
    host_clean(bind_h);
    host_clean(listen_h);
    host_clean(dst_h);
    return 1;
  }

  if (dm) {
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }
    
  main_loop(listen, bind_h, dst_h);

  host_clean(bind_h);
  host_clean(listen_h);
  host_clean(dst_h);

  closelog();

  return 0;
}

/* handle new or known incoming requests */
void handle_inside(int inside, host_t *bind_h, host_t *dst_h, int efd) {
  int len;
  unsigned char buffer[MAX_BUFFER_SIZE];
  struct sockaddr_storage src;
  client_t *client;
  host_t *src_h;
  int output;

while(1) {  // read until empty
  size_t size = sizeof(src);
  len = recvfrom( inside, buffer, sizeof( buffer ), 0,
                  (struct sockaddr*)&src, (socklen_t *)&size );
  if(len == -1) {
     if(errno == EINTR)
        continue;
     if ((errno != EAGAIN) && (errno != EWOULDBLOCK))	
        perror("recvfrom client fd error:");
     /*  all data processed or error. */ 
     return;
  }

  if(len > 0) {
    src_h = get_host(NULL, 0, (struct sockaddr *)&src);
    /* do we know it ? */
    client = client_find_src(src_h);
    if(client != NULL) {
      /* yes, we know it, send req out via existing bind socket */
      verbose("Client %s:%d is known, forwarding %d bytes to %s:%d ",
              host_ip(src_h), host_port(src_h), len, host_ip2(dst_h), host_port(dst_h));
      verb_prbind(bind_h);

      write(client->fd, buffer, len);
      client_seen(client);
      host_clean(src_h);
    }
    else {
      /* unknown client, open new out socket */
      verbose("Client %s:%d is unknown, forwarding %d bytes to %s:%d ",
              host_ip(src_h), host_port(src_h), len, host_ip2(dst_h), host_port(dst_h));
      verb_prbind(bind_h);

      /* if (host_port(bind_h))  // comment out by james@ustc.edu.cn, may cause racecondition when client was cleaned but has data recv
        client_clean(1);
      */
      output = bindsocket(bind_h);
      if (output >= 0) {
        if(connect(output, (struct sockaddr*)&dst_h->ss, sizeof(struct sockaddr_storage)) < 0) {
            fprintf(stderr, "unable to connect to %s:%d\n", host_ip(dst_h), host_port(dst_h));
            perror(NULL);
        }
        else {
          host_t *ret_h;
          struct sockaddr_storage ret;
          /* send req out */
          write(output, buffer, len);
          size = sizeof(struct sockaddr_storage);
          getsockname(output, (struct sockaddr*)&ret, (socklen_t *)&size);
          ret_h = get_host(NULL, 0, (struct sockaddr *)&ret);
          client = client_new(output, src_h, ret_h);

          client_add(client);
          if (set_socket_non_blocking(output) < 0) {
	    perror("error: set_socket_non_blocking");
	    exit(-1);
          }
          struct epoll_event event;
          event.data.ptr = client;
	  event.events = EPOLLIN | EPOLLET;
          if (epoll_ctl(efd, EPOLL_CTL_ADD, output, &event) < 0) {
            perror("error: epoll_ctl_add of newclient");
	  }
          if(LOG) Log("%s:%d(%s:%d)->%s:%d\n",host_ip(src_h),host_port(src_h),host_ip2(ret_h),host_port(ret_h),host_ip3(dst_h),host_port(dst_h));
          verbose("add new client %s:%p %p\n",host_ip(src_h),host_port(src_h),client);	
          host_clean(ret_h);
        }
      }
      host_clean(src_h);
    }
  }
}
}

/* handle answer from the outside */
void handle_outside(int inside, int outside, client_t *client) {
  int len;
  unsigned char buffer[MAX_BUFFER_SIZE];
  struct sockaddr_storage src;
  size_t size;
  if(client == NULL) {
    fprintf(stderr,"what? client == NULL\n");
    exit(1);
  }

while(1) {  // read until empty

  size = sizeof(src);
  
  len = recvfrom( outside, buffer, sizeof( buffer ), 0, (struct sockaddr*)&src, (socklen_t *)&size );
  if(len > 0) {
    /* yes, we know it */
    verbose("recv remote packet for client %p\n",client);	
    sendto(inside, buffer, len, 0, (struct sockaddr*)&client->src.ss, sizeof(struct sockaddr_storage));
    if( ACK_DEL ) {	// got acked packet, then close, for req ack udp, such as DNS
      verbose("got ack, delete client %s:%d %p\n", host_ip(&client->src), host_port(&client->src),client);
      client_close(client);
      return;
    }
  }
  if(len == 0)  {
    fprintf(stderr, "weird, recvfrom returned 0 bytes!\n");
    continue;
  }

  if(len == -1) {
     if(errno == EINTR)
        continue;
     if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
        perror("recvfrom client fd error:");
     /*  all data processed or error */ 
     return;
  }
}
}

int set_socket_non_blocking(int fd) {
  int flags;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return -1;
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) < 0)
     return -1;
   return 0;
}

/* stores system specific information, used by longjmp(), see below */
jmp_buf  JumpBuffer;

/* runs forever, handles incoming requests on the inside and answers on the outside */
int main_loop(int listensocket, host_t *bind_h, host_t *dst_h) {
  int efd;
  struct epoll_event event, *events;

  /* we want to properly tear  down running sessions when interrupted,
     int_handler() will be called on INT or TERM signals */
  signal(SIGINT, int_handler);
  signal(SIGTERM, int_handler);

  if (set_socket_non_blocking(listensocket) < 0) {
	perror("error: set_socket_non_blocking");
	exit(-1);
  }

  if ((efd = epoll_create(10240)) < 0) {
    perror("error: epoll_create1");
    exit(-1);
  }
  event.data.ptr = 0;
  event.events = EPOLLIN | EPOLLET;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, listensocket, &event) < 0) {
    perror("error: epoll_ctl_add of listenfd");
    exit(-1);
  }
  /* Buffer where events are returned */
  events = calloc(MAXEVENTS, sizeof event);
  if (events == NULL) {
    perror("error: calloc memory");
    exit(-1);
  }
  // Event Loop 
  while (1) {
    int n, i;

    /*
      Normally returns 0, that is, if it's the first instruction after
      entering the loop.  However, it  will return 1, when called from
      longjmp(), which will be called by int_handler() if a SIGINT- or
      TERM  arrives.  In  that  case  we leave  the  loop,  tear  down
      everything and exit.
     */
    if (setjmp(JumpBuffer) == 1) {
      break;
    }

    n = epoll_wait(efd, events, MAXEVENTS, -1);
    for (i = 0; i < n; i++) {
      if (0 == events[i].data.ptr) {
      /* incoming client on  the inside, get src, bind  output fd, add
         to list if known, otherwise just handle it */
        handle_inside(listensocket, bind_h, dst_h, efd);
      } else {
        /* remote answer came in on an output fd, proxy back to the inside */
        client_t *client = events[i].data.ptr;
        handle_outside(listensocket, client->fd, client);
      }
    }

    /* close old outputs, if any */
    client_clean(0);
  }
  
  /* we came here via signal handler, clean up */
  close(listensocket);
  client_clean(1);

  return 0;
}

/*
  Handle SIGINT- and TERM, call  longjmp(), which jumps right into the
  main loop, where it causes the loop to be left.
 */
void int_handler(int  sig) {
  signal(sig, SIG_IGN);
  longjmp(JumpBuffer, 1);
}

void verb_prbind (host_t *bind_h) {
  if(VERBOSE) {
    if(strcmp(host_ip(bind_h), "0.0.0.0") != 0 || strcmp(host_ip(bind_h), "[::0]") != 0) {
      verbose("from %s:%d\n", host_ip(bind_h), host_port(bind_h));
    }
    else {
      verbose("\n");
    }
  }
}
