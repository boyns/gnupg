/* misc.c -  miscellaneous functions
 * Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003,
 *               2004 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#if defined(__linux__) && defined(__alpha__) && __GLIBC__ < 2
#include <asm/sysinfo.h>
#include <asm/unistd.h>
#endif
#ifdef HAVE_SETRLIMIT
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include "util.h"
#include "main.h"
#include "photoid.h"
#include "options.h"
#include "i18n.h"

#if defined(__linux__) && defined(__alpha__) && __GLIBC__ < 2
static int
setsysinfo(unsigned long op, void *buffer, unsigned long size,
		     int *start, void *arg, unsigned long flag)
{
    return syscall(__NR_osf_setsysinfo, op, buffer, size, start, arg, flag);
}

void
trap_unaligned(void)
{
    unsigned int buf[2];

    buf[0] = SSIN_UACPROC;
    buf[1] = UAC_SIGBUS | UAC_NOPRINT;
    setsysinfo(SSI_NVPAIRS, buf, 1, 0, 0, 0);
}
#else
void
trap_unaligned(void)
{  /* dummy */
}
#endif


int
disable_core_dumps()
{
#ifdef HAVE_DOSISH_SYSTEM
    return 0;
#else
#ifdef HAVE_SETRLIMIT
    struct rlimit limit;

    limit.rlim_cur = 0;
    limit.rlim_max = 0;
    if( !setrlimit( RLIMIT_CORE, &limit ) )
	return 0;
    if( errno != EINVAL && errno != ENOSYS )
	log_fatal(_("can't disable core dumps: %s\n"), strerror(errno) );
#endif
    return 1;
#endif
}



u16
checksum_u16( unsigned n )
{
    u16 a;

    a  = (n >> 8) & 0xff;
    a += n & 0xff;
    return a;
}


u16
checksum( byte *p, unsigned n )
{
    u16 a;

    for(a=0; n; n-- )
	a += *p++;
    return a;
}

u16
checksum_mpi( MPI a )
{
    u16 csum;
    byte *buffer;
    unsigned nbytes;
    unsigned nbits;

    buffer = mpi_get_buffer( a, &nbytes, NULL );
    nbits = mpi_get_nbits(a);
    csum = checksum_u16( nbits );
    csum += checksum( buffer, nbytes );
    m_free( buffer );
    return csum;
}

u32
buffer_to_u32( const byte *buffer )
{
    unsigned long a;
    a =  *buffer << 24;
    a |= buffer[1] << 16;
    a |= buffer[2] << 8;
    a |= buffer[3];
    return a;
}


static void
no_exp_algo(void)
{
    static int did_note = 0;

    if( !did_note ) {
	did_note = 1;
	log_info(_("Experimental algorithms should not be used!\n"));
    }
}

void
print_pubkey_algo_note( int algo )
{
    if( algo >= 100 && algo <= 110 )
	no_exp_algo();
}

void
print_cipher_algo_note( int algo )
{
    if( algo >= 100 && algo <= 110 )
	no_exp_algo();
    else if(	algo == CIPHER_ALGO_3DES
	     || algo == CIPHER_ALGO_CAST5
	     || algo == CIPHER_ALGO_BLOWFISH
	     || algo == CIPHER_ALGO_TWOFISH
	     || algo == CIPHER_ALGO_AES
	     || algo == CIPHER_ALGO_AES192
	     || algo == CIPHER_ALGO_AES256
	   )
	;
    else {
	static int did_note = 0;

	if( !did_note ) {
	    did_note = 1;
	    log_info(_("this cipher algorithm is deprecated; "
		       "please use a more standard one!\n"));
	}
    }
}

void
print_digest_algo_note( int algo )
{
    if( algo >= 100 && algo <= 110 )
	no_exp_algo();
}


/* Return a string which is used as a kind of process ID */
const byte *
get_session_marker( size_t *rlen )
{
    static byte marker[SIZEOF_UNSIGNED_LONG*2];
    static int initialized;

    if ( !initialized ) {
        volatile ulong aa, bb; /* we really want the uninitialized value */
        ulong a, b;

        initialized = 1;
        /* also this marker is guessable it is not easy to use this 
         * for a faked control packet because an attacker does not
         * have enough control about the time the verification does 
         * take place.  Of course, we can add just more random but 
         * than we need the random generator even for verification
         * tasks - which does not make sense. */
        a = aa ^ (ulong)getpid();
        b = bb ^ (ulong)time(NULL);
        memcpy( marker, &a, SIZEOF_UNSIGNED_LONG );
        memcpy( marker+SIZEOF_UNSIGNED_LONG, &b, SIZEOF_UNSIGNED_LONG );
    }
    *rlen = sizeof(marker);
    return marker;
}

/****************
 * Wrapper around the libgcrypt function with addional checks on
 * openPGP contraints for the algo ID.
 */
int
openpgp_cipher_test_algo( int algo )
{
    if( algo < 0 || algo > 110 )
        return G10ERR_CIPHER_ALGO;
    return check_cipher_algo(algo);
}

int
openpgp_pk_test_algo( int algo, unsigned int usage_flags )
{
    if( algo < 0 || algo > 110 )
	return G10ERR_PUBKEY_ALGO;
    return check_pubkey_algo2( algo, usage_flags );
}

int 
openpgp_pk_algo_usage ( int algo )
{
    int use = 0; 
    
    /* they are hardwired in gpg 1.0 */
    switch ( algo ) {    
      case PUBKEY_ALGO_RSA:
          use = PUBKEY_USAGE_SIG | PUBKEY_USAGE_ENC | PUBKEY_USAGE_AUTH;
          break;
      case PUBKEY_ALGO_RSA_E:
          use = PUBKEY_USAGE_ENC;
          break;
      case PUBKEY_ALGO_RSA_S:
          use = PUBKEY_USAGE_SIG;
          break;
      case PUBKEY_ALGO_ELGAMAL_E:
          use = PUBKEY_USAGE_ENC;
          break;
      case PUBKEY_ALGO_DSA:  
          use = PUBKEY_USAGE_SIG | PUBKEY_USAGE_AUTH;
          break;
      default:
          break;
    }
    return use;
}

int
openpgp_md_test_algo( int algo )
{
    if( algo < 0 || algo > 110 )
        return G10ERR_DIGEST_ALGO;
    return check_digest_algo(algo);
}

#ifdef USE_IDEA
/* Special warning for the IDEA cipher */
void
idea_cipher_warn(int show)
{
  static int warned=0;

  if(!warned || show)
    {
      log_info(_("the IDEA cipher plugin is not present\n"));
      log_info(_("please see http://www.gnupg.org/why-not-idea.html "
		 "for more information\n"));
      warned=1;
    }
}
#endif

/* Expand %-strings.  Returns a string which must be m_freed.  Returns
   NULL if the string cannot be expanded (too large). */
char *
pct_expando(const char *string,struct expando_args *args)
{
  const char *ch=string;
  int idx=0,maxlen=0,done=0;
  u32 pk_keyid[2]={0,0},sk_keyid[2]={0,0};
  char *ret=NULL;

  if(args->pk)
    keyid_from_pk(args->pk,pk_keyid);

  if(args->sk)
    keyid_from_sk(args->sk,sk_keyid);

  /* This is used so that %k works in photoid command strings in
     --list-secret-keys (which of course has a sk, but no pk). */
  if(!args->pk && args->sk)
    keyid_from_sk(args->sk,pk_keyid);

  while(*ch!='\0')
    {
      char *str=NULL;

      if(!done)
	{
	  /* 8192 is way bigger than we'll need here */
	  if(maxlen>=8192)
	    goto fail;

	  maxlen+=1024;
	  ret=m_realloc(ret,maxlen);
	}

      done=0;

      if(*ch=='%')
	{
	  switch(*(ch+1))
	    {
	    case 's': /* short key id */
	      if(idx+8<maxlen)
		{
		  sprintf(&ret[idx],"%08lX",(ulong)sk_keyid[1]);
		  idx+=8;
		  done=1;
		}
	      break;

	    case 'S': /* long key id */
	      if(idx+16<maxlen)
		{
		  sprintf(&ret[idx],"%08lX%08lX",
			  (ulong)sk_keyid[0],(ulong)sk_keyid[1]);
		  idx+=16;
		  done=1;
		}
	      break;

	    case 'k': /* short key id */
	      if(idx+8<maxlen)
		{
		  sprintf(&ret[idx],"%08lX",(ulong)pk_keyid[1]);
		  idx+=8;
		  done=1;
		}
	      break;

	    case 'K': /* long key id */
	      if(idx+16<maxlen)
		{
		  sprintf(&ret[idx],"%08lX%08lX",
			  (ulong)pk_keyid[0],(ulong)pk_keyid[1]);
		  idx+=16;
		  done=1;
		}
	      break;

	    case 'p': /* primary pk fingerprint of a sk */
	    case 'f': /* pk fingerprint */
	    case 'g': /* sk fingerprint */
	      {
		byte array[MAX_FINGERPRINT_LEN];
		size_t len;
		int i;

		if((*(ch+1))=='p' && args->sk)
		  {
		    if(args->sk->is_primary)
		      fingerprint_from_sk(args->sk,array,&len);
		    else if(args->sk->main_keyid[0] || args->sk->main_keyid[1])
		      {
			PKT_public_key *pk=
			  m_alloc_clear(sizeof(PKT_public_key));

			if(get_pubkey_fast(pk,args->sk->main_keyid)==0)
			  fingerprint_from_pk(pk,array,&len);
			else
			  memset(array,0,(len=MAX_FINGERPRINT_LEN));
			free_public_key(pk);
		      }
		    else
		      memset(array,0,(len=MAX_FINGERPRINT_LEN));
		  }
		else if((*(ch+1))=='f' && args->pk)
		  fingerprint_from_pk(args->pk,array,&len);
		else if((*(ch+1))=='g' && args->sk)
		  fingerprint_from_sk(args->sk,array,&len);
		else
		  memset(array,0,(len=MAX_FINGERPRINT_LEN));

		if(idx+(len*2)<maxlen)
		  {
		    for(i=0;i<len;i++)
		      {
			sprintf(&ret[idx],"%02X",array[i]);
			idx+=2;
		      }
		    done=1;
		  }
	      }
	      break;

	    case 't': /* e.g. "jpg" */
	      str=image_type_to_string(args->imagetype,0);
	      /* fall through */

	    case 'T': /* e.g. "image/jpeg" */
	      if(str==NULL)
		str=image_type_to_string(args->imagetype,2);

	      if(idx+strlen(str)<maxlen)
		{
		  strcpy(&ret[idx],str);
		  idx+=strlen(str);
		  done=1;
		}
	      break;

	    case '%':
	      if(idx+1<maxlen)
		{
		  ret[idx++]='%';
		  ret[idx]='\0';
		  done=1;
		}
	      break;

	      /* Any unknown %-keys (like %i, %o, %I, and %O) are
		 passed through for later expansion.  Note this also
		 handles the case where the last character in the
		 string is a '%' - the terminating \0 will end up here
		 and properly terminate the string. */
	    default:
	      if(idx+2<maxlen)
		{
		  ret[idx++]='%';
		  ret[idx++]=*(ch+1);
		  ret[idx]='\0';
		  done=1;
		}
	      break;
	      }

	  if(done)
	    ch++;
	}
      else
	{
	  if(idx+1<maxlen)
	    {
	      ret[idx++]=*ch;
	      ret[idx]='\0';
	      done=1;
	    }
	}

      if(done)
	ch++;
    }

  return ret;

 fail:
  m_free(ret);
  return NULL;
}

void
deprecated_warning(const char *configname,unsigned int configlineno,
		   const char *option,const char *repl1,const char *repl2)
{
  if(configname)
    {
      if(strncmp("--",option,2)==0)
	option+=2;

      if(strncmp("--",repl1,2)==0)
	repl1+=2;

      log_info(_("%s:%d: deprecated option \"%s\"\n"),
	       configname,configlineno,option);
    }
  else
    log_info(_("WARNING: \"%s\" is a deprecated option\n"),option);

  log_info(_("please use \"%s%s\" instead\n"),repl1,repl2);
}

const char *
compress_algo_to_string(int algo)
{
  const char *s=NULL;

  switch(algo)
    {
    case COMPRESS_ALGO_NONE:
      s=_("Uncompressed");
      break;

    case COMPRESS_ALGO_ZIP:
      s="ZIP";
      break;

    case COMPRESS_ALGO_ZLIB:
      s="ZLIB";
      break;

#ifdef HAVE_BZIP2
    case COMPRESS_ALGO_BZIP2:
      s="BZIP2";
      break;
#endif
    }

  return s;
}

int
string_to_compress_algo(const char *string)
{
  /* NOTE TO TRANSLATOR: See doc/TRANSLATE about this string. */
  if(match_multistr(_("uncompressed|none"),string))
    return 0;
  else if(ascii_strcasecmp(string,"uncompressed")==0)
    return 0;
  else if(ascii_strcasecmp(string,"none")==0)
    return 0;
  else if(ascii_strcasecmp(string,"zip")==0)
    return 1;
  else if(ascii_strcasecmp(string,"zlib")==0)
    return 2;
#ifdef HAVE_BZIP2
  else if(ascii_strcasecmp(string,"bzip2")==0)
    return 3;
#endif
  else if(ascii_strcasecmp(string,"z0")==0)
    return 0;
  else if(ascii_strcasecmp(string,"z1")==0)
    return 1;
  else if(ascii_strcasecmp(string,"z2")==0)
    return 2;
#ifdef HAVE_BZIP2
  else if(ascii_strcasecmp(string,"z3")==0)
    return 3;
#endif
  else
    return -1;
}

int
check_compress_algo(int algo)
{
#ifdef HAVE_BZIP2
  if(algo>=0 && algo<=3)
    return 0;
#else
  if(algo>=0 && algo<=2)
    return 0;
#endif

  return G10ERR_COMPR_ALGO;
}

int
default_cipher_algo(void)
{
  if(opt.def_cipher_algo)
    return opt.def_cipher_algo;
  else if(opt.personal_cipher_prefs)
    return opt.personal_cipher_prefs[0].value;
  else
    return opt.s2k_cipher_algo;
}

/* There is no default_digest_algo function, but see
   sign.c:hash_for() */

int
default_compress_algo(void)
{
  if(opt.compress_algo!=-1)
    return opt.compress_algo;
  else if(opt.personal_compress_prefs)
    return opt.personal_compress_prefs[0].value;
  else
    return DEFAULT_COMPRESS_ALGO;
}

const char *
compliance_option_string(void)
{
  switch(opt.compliance)
    {
    case CO_RFC2440:
      return "--openpgp";
    case CO_PGP2:
      return "--pgp2";
    case CO_PGP6:
      return "--pgp6";
    case CO_PGP7:
      return "--pgp7";
    case CO_PGP8:
      return "--pgp8";
    default:
      return "???";
    }
}

static const char *
compliance_string(void)
{
  switch(opt.compliance)
    {
    case CO_RFC2440:
      return "OpenPGP";
    case CO_PGP2:
      return "PGP 2.x";
    case CO_PGP6:
      return "PGP 6.x";
    case CO_PGP7:
      return "PGP 7.x";
    case CO_PGP8:
      return "PGP 8.x";
    default:
      return "???";
    }
}

void
compliance_failure(void)
{
  log_info(_("this message may not be usable by %s\n"),compliance_string());
  opt.compliance=CO_GNUPG;
}

char *
argsep(char **stringp,char **arg)
{
  char *tok,*next;

  tok=*stringp;
  *arg=NULL;

  if(tok)
    {
      next=strpbrk(tok," ,=");

      if(next)
	{
	  int sawequals=0;

	  if(*next=='=')
	    sawequals=1;

	  *next++='\0';
	  *stringp=next;

	  /* what we need to do now is scan along starting with *next.
	     If the next character we see (ignoring spaces) is a =
	     sign, then there is an argument. */

	  while(*next)
	    {
	      if(*next=='=')
		sawequals=1;
	      else if(*next!=' ')
		break;
	      next++;
	    }

	  /* At this point, *next is either an empty string, or the
	     beginning of the next token (which is an argument if
	     sawequals is true). */

	  if(sawequals)
	    {
	      *arg=next;
	      next=strpbrk(*arg," ,");
	      if(next)
		{
		  *next++='\0';
		  *stringp=next;
		}
	      else
		*stringp=NULL;
	    }
	}
      else
	*stringp=NULL;
    }

  return tok;
}

int
parse_options(char *str,unsigned int *options,
	      struct parse_options *opts,int noisy)
{
  char *tok,*arg;

  while((tok=argsep(&str,&arg)))
    {
      int i,rev=0;
      char *otok=tok;

      if(tok[0]=='\0')
	continue;

      if(ascii_strncasecmp("no-",tok,3)==0)
	{
	  rev=1;
	  tok+=3;
	}

      for(i=0;opts[i].name;i++)
	{
	  size_t toklen=strlen(tok);

	  if(ascii_strncasecmp(opts[i].name,tok,toklen)==0)
	    {
	      /* We have a match, but it might be incomplete */
	      if(toklen!=strlen(opts[i].name))
		{
		  int j;

		  for(j=i+1;opts[j].name;j++)
		    {
		      if(ascii_strncasecmp(opts[j].name,tok,toklen)==0)
			{
			  if(noisy)
			    log_info(_("ambiguous option `%s'\n"),otok);
			  return 0;
			}
		    }
		}

	      if(rev)
		{
		  *options&=~opts[i].bit;
		  if(opts[i].value)
		    *opts[i].value=NULL;
		}
	      else
		{
		  *options|=opts[i].bit;
		  if(opts[i].value)
		    *opts[i].value=arg?m_strdup(arg):NULL;
		}
	      break;
	    }
	}

      if(!opts[i].name)
	{
	  if(noisy)
	    log_info(_("unknown option `%s'\n"),otok);
	  return 0;
	}
    }

  return 1;
}
