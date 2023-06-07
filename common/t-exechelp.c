/* t-exechelp.c - Module test for exechelp.c
 *	Copyright (C) 2009 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "util.h"
#include "exechelp.h"

static int verbose;

#ifndef HAVE_W32_SYSTEM
static void
print_open_fds (int *array)
{
  int n;

  if (!verbose)
    return;

  for (n=0; array[n] != -1; n++)
    ;
  printf ("open file descriptors: %d", n);
  putchar (' ');
  putchar (' ');
  putchar ('(');
  for (n=0; array[n] != -1; n++)
    printf ("%d%s", array[n], array[n+1] == -1?"":" ");
  putchar (')');
  putchar ('\n');
}


static int *
xget_all_open_fds (void)
{
  int *array;

  array = get_all_open_fds ();
  if (!array)
    {
      fprintf (stderr, "%s:%d: get_all_open_fds failed: %s\n",
               __FILE__, __LINE__, strerror (errno));
      exit (1);
    }
  return array;
}


/* That is a very crude test.  To do a proper test we would need to
   fork a test process and best return information by some other means
   than file descriptors. */
static void
test_close_all_fds (void)
{
  int max_fd = get_max_fds ();
  int *array;
  int fd;
  int initial_count, count, n;
#if 0
  char buffer[100];

  snprintf (buffer, sizeof buffer, "/bin/ls -l /proc/%d/fd", (int)getpid ());
  system (buffer);
#endif

  if (verbose)
    printf ("max. file descriptors: %d\n", max_fd);
  array = xget_all_open_fds ();
  print_open_fds (array);
  for (initial_count=n=0; array[n] != -1; n++)
    initial_count++;
  free (array);

  /* Some dups to get more file descriptors and close one. */
  dup (1);
  dup (1);
  fd = dup (1);
  dup (1);
  close (fd);

  array = xget_all_open_fds ();
  if (verbose)
    print_open_fds (array);
  for (count=n=0; array[n] != -1; n++)
    count++;
  if (count != initial_count+3)
    {
      fprintf (stderr, "%s:%d: dup or close failed\n",
               __FILE__, __LINE__);
      exit (1);
    }
  free (array);

  /* Close the non standard ones.  */
  close_all_fds (3, NULL);

  /* Get a list to check whether they are all closed.  */
  array = xget_all_open_fds ();
  if (verbose)
    print_open_fds (array);
  for (count=n=0; array[n] != -1; n++)
    count++;
  if (count > initial_count)
    {
      fprintf (stderr, "%s:%d: not all files were closed\n",
               __FILE__, __LINE__);
      exit (1);
    }
  initial_count = count;
  free (array);

  /* Now let's check the realloc we use.  We do this and the next
     tests only if we are allowed to open enough descriptors.  */
  if (get_max_fds () > 32)
    {
      int except[] = { 20, 23, 24, -1 };

      for (n=initial_count; n < 31; n++)
        dup (1);
      array = xget_all_open_fds ();
      if (verbose)
        print_open_fds (array);
      free (array);
      for (n=0; n < 5; n++)
        {
          dup (1);
          array = xget_all_open_fds ();
          if (verbose)
            print_open_fds (array);
          free (array);
        }

      /* Check whether the except list works.  */
      close_all_fds (3, except);
      array = xget_all_open_fds ();
      if (verbose)
        print_open_fds (array);
      for (count=n=0; array[n] != -1; n++)
        count++;
      free (array);

      if (count != initial_count + DIM(except)-1)
        {
          fprintf (stderr, "%s:%d: close_all_fds failed\n",
                   __FILE__, __LINE__);
          exit (1);
        }
    }

}
#endif

static char buff12k[1024*12];

static void
run_server (void)
{
  estream_t fp;
  int i;
  char *p;
  unsigned int len;
  int ret;

  fp = es_fdopen_nc (1, "w");
  if (fp == NULL)
    {
      fprintf (stderr, "es_fdopen failed\n");
      exit (1);
    }

  /* Fill the buffer by ASCII chars.  */
  p = buff12k;
  for (i = 0; i < sizeof (buff12k); i++)
    if ((i % 64) == 63)
      *p++ = '\n';
    else
      *p++ = (i % 64) + '@';

  len = sizeof (buff12k);

  ret = es_write (fp, (void *)&len, sizeof (len), NULL);
  if (ret)
    {
      fprintf (stderr, "es_write (1) failed\n");
      exit (1);
    }

  ret = es_write (fp, buff12k, sizeof (buff12k), NULL);
  if (ret)
    {
      fprintf (stderr, "es_write (2) failed\n");
      exit (1);
    }
  es_fclose (fp);
  exit (0);
}


static void
test_pipe_stream (const char *pgmname)
{
  gpg_error_t err;
  gnupg_process_t proc;
  estream_t outfp;
  const char *argv[2];
  unsigned int len;
  size_t n;
  int ret;

  argv[0] = "--server";
  argv[1] = NULL;

  err = gnupg_process_spawn (pgmname, argv,
                             (GNUPG_PROCESS_STDOUT_PIPE
                              |GNUPG_PROCESS_STDERR_KEEP),
                             NULL, NULL, &proc);
  if (err)
    {
      fprintf (stderr, "gnupg_process_spawn failed\n");
      exit (1);
    }

  gnupg_process_get_streams (proc, 0, NULL, &outfp, NULL);

  ret = es_read (outfp, (void *)&len, sizeof (len), NULL);
  if (ret)
    {
      fprintf (stderr, "es_read (1) failed\n");
      exit (1);
    }
  ret = es_read (outfp, buff12k, sizeof (buff12k), &n);
  if (ret || n != sizeof (buff12k))
    {
      fprintf (stderr, "es_read (2) failed\n");
      exit (1);
    }
  es_fclose (outfp);
  gnupg_process_release (proc);
}


int
main (int argc, char **argv)
{
  const char *myname = "no-pgm";

  if (argc)
    {
      myname = argv[0];
      argc--; argv++;
    }
  if (argc && !strcmp (argv[0], "--verbose"))
    {
      verbose = 1;
      argc--; argv++;
    }
  if (argc && !strcmp (argv[0], "--server"))
    run_server ();

#ifndef HAVE_W32_SYSTEM
  test_close_all_fds ();
#endif
  test_pipe_stream (myname);

  return 0;
}
