/**
 * Object oriented C module to send ICMP and ICMPv6 `echo's.
 * Copyright (C) 2006-2017  Florian octo Forster <ff at octo.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; only version 2 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <assert.h>

#include <unistd.h>

#include <math.h>

#include <sys/time.h>
#include <time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <netdb.h> /* NI_MAXHOST */

#include <signal.h>

#include <sys/types.h>

#include <locale.h>
#include <langinfo.h>

#define PACKAGE_VERSION "1.0"

#undef USE_NCURSES

#include "oping.h"

#ifndef IPTOS_MINCOST
# define IPTOS_MINCOST 0x02
#endif

/* Remove GNU specific __attribute__ settings when using another compiler */
#if !__GNUC__
# define __attribute__(x) /**/
#endif

typedef struct ping_context
{
	char host[NI_MAXHOST];
	char addr[NI_MAXHOST];

	int index;
	int req_sent;
	int req_rcvd;

	double latency_total;

} ping_context_t;

static double  opt_timeout    = PING_DEF_TIMEOUT;
static char   *opt_device     = NULL;
static char   *opt_mark       = NULL;
static int     opt_count      = -1;
static int     opt_send_ttl   = 64;
static const uint8_t opt_send_qos   = 0;
static int     opt_bell       = 0;

static int host_num  = 0;

static ping_context_t *context_create () /* {{{ */
{
  ping_context_t *ctx = (ping_context_t *)calloc (1, sizeof (*ctx));
	if (ctx == NULL)
		return (NULL);

	return (ctx);
} /* }}} ping_context_t *context_create */

static void context_destroy (ping_context_t *context) /* {{{ */
{
	if (context == NULL)
		return;

	free (context);
} /* }}} void context_destroy */

static double context_get_packet_loss (const ping_context_t *ctx) /* {{{ */
{
	if (ctx == NULL)
		return (-1.0);

	if (ctx->req_sent < 1)
		return (0.0);

	return (100.0 * (ctx->req_sent - ctx->req_rcvd)
			/ ((double) ctx->req_sent));
} /* }}} double context_get_packet_loss */

static int ping_initialize_contexts (pingobj *ping) /* {{{ */
{
	pingobj_iter_t *iter;
	int index;

	if (ping == NULL)
		return (EINVAL);

	index = 0;
	for (iter = ping_iterator_get (ping);
			iter != NULL;
			iter = ping_iterator_next (iter))
	{
		ping_context_t *context;
		size_t buffer_size;

		context = (ping_context_t *)ping_iterator_get_context(iter);

		/* if this is a previously existing host, do not recreate it */
		if (context != NULL)
		{
			context->index = index++;
			continue;
		}

		context = context_create ();
		context->index = index;

		buffer_size = sizeof (context->host);
		ping_iterator_get_info (iter, PING_INFO_HOSTNAME, context->host, &buffer_size);

		buffer_size = sizeof (context->addr);
		ping_iterator_get_info (iter, PING_INFO_ADDRESS, context->addr, &buffer_size);

		ping_iterator_set_context (iter, (void *) context);

		index++;
	}

	return (0);
} /* }}} int ping_initialize_contexts */

static void usage_exit (const char *name, int status) /* {{{ */
{
	fprintf (stderr, "Usage: %s [OPTIONS] "
				"-f filename | host [host [host ...]]\n"

			"\nAvailable options:\n"
			"  -c count     number of ICMP packets to send\n"
			"  -w timeout   time to wait for replies, in seconds\n"
			"  -t ttl       time to live for each ICMP packet\n"
			"  -I srcaddr   source address\n"
			"  -D device    outgoing interface name\n"
			"  -m mark      mark to set on outgoing packets\n"
			"  -P percent   Report the n'th percentile of latency\n"
			"  -Z percent   Exit with non-zero exit status if more than this percentage of\n"
			"               probes timed out. (default: never)\n"

			"\noping " PACKAGE_VERSION ", http://noping.cc/\n"
			"by Florian octo Forster <ff@octo.it>\n"
			"for contributions see `AUTHORS'\n",
			name);
	exit (status);
} /* }}} void usage_exit */

static int read_options (int argc, char **argv) /* {{{ */
{
	int optchar;

	while (1)
	{
		optchar = getopt (argc, argv, "c:hi:I:t:Q:f:D:Z:O:P:m:w:b"
				);

		if (optchar == -1)
			break;

		switch (optchar)
		{
			case 'c':
				{
					int new_count;
					new_count = atoi (optarg);
					if (new_count > 0)
					{
						opt_count = new_count;
					}
					else
						fprintf(stderr, "Ignoring invalid count: %s\n",
								optarg);
				}
				break;

			case 'w':
				{
					char *endp = NULL;
					double t = strtod (optarg, &endp);
					if ((optarg[0] != 0) && (endp != NULL) && (*endp == 0))
						opt_timeout = t;
					else
						fprintf (stderr, "Ignoring invalid timeout: %s\n",
								optarg);
				}
				break;

			case 'D':
				opt_device = optarg;
				break;

			case 'm':
				opt_mark = optarg;
				break;

			case 't':
			{
				int new_send_ttl;
				new_send_ttl = atoi (optarg);
				if ((new_send_ttl > 0) && (new_send_ttl < 256))
					opt_send_ttl = new_send_ttl;
				else
					fprintf (stderr, "Ignoring invalid TTL argument: %s\n",
							optarg);
				break;
			}

			case 'b':
				opt_bell = 1;
				break;

			case 'h':
				usage_exit (argv[0], 0);
				break;

			default:
				usage_exit (argv[0], 1);
		}
	}

	return (optind);
} /* }}} read_options */

static int pre_loop_hook (pingobj *ping) /* {{{ */
{
	pingobj_iter_t *iter;

	for (iter = ping_iterator_get (ping);
			iter != NULL;
			iter = ping_iterator_next (iter))
	{
		ping_context_t *ctx;
		size_t buffer_size;

		ctx = (ping_context_t *)ping_iterator_get_context (iter);
		if (ctx == NULL)
			continue;

		buffer_size = 0;
		ping_iterator_get_info (iter, PING_INFO_DATA, NULL, &buffer_size);

		printf ("PING %s (%s) %zu bytes of data.\n",
				ctx->host, ctx->addr, buffer_size);
	}

	return (0);
} /* }}} int pre_loop_hook */

static void update_context (ping_context_t *ctx, double latency) /* {{{ */
{
	ctx->req_sent++;

	if (latency > 0.0)
	{
		ctx->req_rcvd++;
		ctx->latency_total += latency;
	}
	else
	{
		latency = NAN;
	}
} /* }}} void update_context */

static void update_host_hook (pingobj_iter_t *iter, /* {{{ */
                              __attribute__((unused)) int index) {
	double          latency;
	unsigned int    sequence;
	int             recv_ttl;
	uint8_t         recv_qos;
	size_t          buffer_len;
	size_t          data_len;
	ping_context_t *context;

	latency = -1.0;
	buffer_len = sizeof (latency);
	ping_iterator_get_info (iter, PING_INFO_LATENCY,
			&latency, &buffer_len);

	sequence = 0;
	buffer_len = sizeof (sequence);
	ping_iterator_get_info (iter, PING_INFO_SEQUENCE,
			&sequence, &buffer_len);

	recv_ttl = -1;
	buffer_len = sizeof (recv_ttl);
	ping_iterator_get_info (iter, PING_INFO_RECV_TTL,
			&recv_ttl, &buffer_len);

	recv_qos = 0;
	buffer_len = sizeof (recv_qos);
	ping_iterator_get_info (iter, PING_INFO_RECV_QOS,
			&recv_qos, &buffer_len);

	data_len = 0;
	ping_iterator_get_info (iter, PING_INFO_DATA,
			NULL, &data_len);

	context = (ping_context_t *) ping_iterator_get_context (iter);

# define HOST_PRINTF(...) printf(__VA_ARGS__)

	update_context (context, latency);

	if (latency > 0.0)
	{
		HOST_PRINTF ("%zu bytes from %s (%s): icmp_seq=%u ttl=%i ",
				data_len,
				context->host, context->addr,
				sequence, recv_ttl);
		HOST_PRINTF ("time=%.2f ms\n", latency);
    if (opt_bell) {
			HOST_PRINTF ("\a");
    }
	}
	else /* if (!(latency > 0.0)) */
	{
		HOST_PRINTF ("echo reply from %s (%s): icmp_seq=%u timeout\n",
				context->host, context->addr,
				sequence);
	}

} /* }}} void update_host_hook */

/* Prints statistics for each host, cleans up the contexts */
static int post_loop_hook (pingobj *ping) {
  pingobj_iter_t *iter;
  int failure_count = 0;

  for (iter = ping_iterator_get (ping);
	   iter != NULL;
	   iter = ping_iterator_next (iter)) {
	ping_context_t *context =
	  (ping_context_t *)ping_iterator_get_context (iter);

	printf ("\n--- %s ping statistics ---\n"
			"%i packets transmitted, %i received, %.2f%% packet loss, "
			"time %.1fms\n",
			context->host, context->req_sent, context->req_rcvd,
			context_get_packet_loss (context),
			context->latency_total);

	ping_iterator_set_context (iter, NULL);
	context_destroy (context);
  }

  return failure_count;
}

int main (int argc, char **argv) {
  pingobj      *ping;
  pingobj_iter_t *iter;

  int optind;
  int i;
  int status;

  setlocale(LC_ALL, "");
  optind = read_options (argc, argv);

  if (optind >= argc) {
	usage_exit (argv[0], 1);
  }

  if ((ping = ping_construct ()) == NULL) {
	fprintf (stderr, "ping_construct failed\n");
	return (1);
  }

  if (ping_setopt (ping, PING_OPT_TTL, &opt_send_ttl) != 0) {
	fprintf (stderr, "Setting TTL to %i failed: %s\n",
			 opt_send_ttl, ping_get_error (ping));
  }

  if (ping_setopt (ping, PING_OPT_QOS, &opt_send_qos) != 0) {
	fprintf (stderr, "Setting TOS to %i failed: %s\n",
			 opt_send_qos, ping_get_error (ping));
  }

  if (ping_setopt (ping, PING_OPT_TIMEOUT, (void*)(&opt_timeout)) != 0) {
	fprintf (stderr, "Setting timeout failed: %s\n",
			 ping_get_error (ping));
  }

  if (opt_device != NULL) {
	if (ping_setopt (ping, PING_OPT_DEVICE, (void *) opt_device) != 0) {
	  fprintf (stderr, "Setting device failed: %s\n",
			   ping_get_error (ping));
	}
  }

  if (opt_mark != NULL) {
	char *endp = NULL;
	int mark = (int) strtol (opt_mark, &endp, /* base = */ 0);
	if ((opt_mark[0] != 0) && (endp != NULL) && (*endp == 0)) {
	  if (ping_setopt(ping, PING_OPT_MARK, (void*)(&mark)) != 0) {
		fprintf (stderr, "Setting mark failed: %s\n",
				 ping_get_error (ping));
	  }
	} else {
	  fprintf(stderr, "Ignoring invalid mark: %s\n", optarg);
	}
  }

  for (i = optind; i < argc; i++) {
	if (ping_host_add (ping, argv[i]) < 0) {
	  const char *errmsg = ping_get_error (ping);

	  fprintf (stderr, "Adding host `%s' failed: %s\n", argv[i], errmsg);
	  continue;
	} else {
	  host_num++;
	}
  }

  /* Permanently drop root privileges if we're setuid-root. */
  status = setuid (getuid ());
  if (status != 0) {
	fprintf (stderr, "Dropping privileges failed: %s\n",
			 strerror (errno));
	exit (EXIT_FAILURE);
  }

  if (host_num == 0)
	exit (EXIT_FAILURE);

  ping_initialize_contexts (ping);

  if (i == 0)
	return (1);

  pre_loop_hook (ping);

  while (opt_count != 0) {
	if (ping_send (ping) < 0) {
	  fprintf (stderr, "ping_send failed: %s\n",
			   ping_get_error (ping));
	  continue;
	}

	int index = 0;
	for (iter = ping_iterator_get (ping);
		 iter != NULL;
		 iter = ping_iterator_next (iter)) {
	  update_host_hook (iter, index);
	  index++;
	}

	/* Don't sleep in the last iteration */
	if (opt_count == 1)
	  break;

	sleep(1);

	if (opt_count > 0)
	  opt_count--;
  }

  /* Returns the number of failed hosts according to -Z. */
  status = post_loop_hook (ping);

  ping_destroy (ping);

  return 0;
}

