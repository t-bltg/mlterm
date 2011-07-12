/*
 *	$Id$
 */

#include  "ml_pty_intern.h"

#include  <stdio.h>		/* sprintf */
#include  <unistd.h>		/* close */
#include  <sys/ioctl.h>
#include  <termios.h>
#include  <signal.h>		/* signal/SIGWINCH */
#include  <string.h>		/* strchr/memcpy */
#include  <stdlib.h>		/* putenv */
#include  <kiklib/kik_debug.h>
#include  <kiklib/kik_mem.h>	/* realloc/alloca */
#include  <kiklib/kik_str.h>	/* strdup */
#include  <kiklib/kik_pty.h>
#include  <kiklib/kik_path.h>	/* kik_basename */
#ifdef  USE_UTMP
#include  <kiklib/kik_utmp.h>
#endif


#if  0
#define  __DEBUG
#endif


typedef struct ml_pty_unix
{
	ml_pty_t  pty ;
#ifdef  USE_UTMP
	kik_utmp_t *  utmp ;
#endif

} ml_pty_unix_t ;


/* --- static functions --- */

static int
delete(
	ml_pty_t *  pty
	)
{
#ifdef  USE_UTMP
	if( ((ml_pty_unix_t*)pty)->utmp)
	{
		kik_utmp_delete( ((ml_pty_unix_t*)pty)->utmp) ;
	}
#endif

#ifdef  __DEBUG
	kik_debug_printf( "%d fd is closed\n" , pty->master) ;
#endif

	kik_pty_close( pty->master) ;
	close( pty->slave) ;

	free( pty) ;

	return  1 ;
}

static int
set_winsize(
	ml_pty_t *  pty ,
	u_int  cols ,
	u_int  rows
	)
{
	struct winsize  ws ;

#ifdef  __DEBUG
	kik_debug_printf( KIK_DEBUG_TAG " win size cols %d rows %d.\n" , cols , rows) ;
#endif

	ws.ws_col = cols ;
	ws.ws_row = rows ;
	ws.ws_xpixel = 0 ;
	ws.ws_ypixel = 0 ;
	
	if( ioctl( pty->master , TIOCSWINSZ , &ws) < 0)
	{
	#ifdef  DBEUG
		kik_warn_printf( KIK_DEBUG_TAG " ioctl(TIOCSWINSZ) failed.\n") ;
	#endif
	
		return  0 ;
	}

	kill( pty->child_pid , SIGWINCH) ;

	return  1 ;
}

static ssize_t
write_to_pty(
	ml_pty_t *  pty ,
	u_char *  buf ,
	size_t  len
	)
{
	return  write( pty->master , buf , len) ;
}

static ssize_t
read_pty(
	ml_pty_t *  pty ,
	u_char *  buf ,
	size_t  len
	)
{
	return  read( pty->master , buf , len) ;
}


/* --- global functions --- */

ml_pty_t *
ml_pty_unix_new(
	const char *  cmd_path ,	/* can be NULL */
	char **  cmd_argv ,	/* can be NULL(only if cmd_path is NULL) */
	char **  env ,		/* can be NULL */
	const char *  host ,
	u_int  cols ,
	u_int  rows
	)
{
	ml_pty_t *  pty ;
	pid_t  pid ;

	if( ( pty = malloc( sizeof( ml_pty_unix_t))) == NULL)
	{
		return  NULL ;
	}

	pid = kik_pty_fork( &pty->master , &pty->slave) ;

	if( pid == -1)
	{
		return  NULL ;
	}

	if( pid == 0)
	{
		/* child process */

		/* reset signals and spin off the command interpreter */
		signal(SIGINT, SIG_DFL) ;
		signal(SIGQUIT, SIG_DFL) ;
		signal(SIGCHLD, SIG_DFL) ;
		signal(SIGPIPE, SIG_DFL) ;

		if( ! cmd_path)
		{
			pty->child_pid = 0 ;
			pty->buf = NULL ;
			pty->left = 0 ;
			pty->size = 0 ;
			pty->pty_listener = NULL ;
			
		#ifdef  USE_UTMP
			((ml_pty_unix_t*)pty)->utmp = NULL ;
		#endif

			return  pty ;
		}
		
		/*
		 * setting environmental variables.
		 */
		if( env)
		{
			while( *env)
			{
				/*
				 * an argument string of putenv() must be allocated memory.
				 * (see SUSV2)
				 */
				putenv( strdup( *env)) ;

				env ++ ;
			}
		}

	#if  0
		/*
		 * XXX is this necessary ?
		 *
		 * mimick login's behavior by disabling the job control signals.
		 * a shell that wants them can turn them back on
		 */
		signal(SIGTSTP , SIG_IGN) ;
		signal(SIGTTIN , SIG_IGN) ;
		signal(SIGTTOU , SIG_IGN) ;
	#endif

		if( strchr( cmd_path , '/') == NULL)
		{
			if( execvp( cmd_path , cmd_argv) < 0)
			{
			#ifdef  DEBUG
				kik_warn_printf( KIK_DEBUG_TAG " execve(%s) failed.\n" ,
					cmd_path) ;
			#endif
			}
		}
		else
		{
			if( execv( cmd_path , cmd_argv) < 0)
			{
			#ifdef  DEBUG
				kik_warn_printf( KIK_DEBUG_TAG " execve(%s) failed.\n" ,
					cmd_path) ;
			#endif
			
				exit(1) ;
			}
		}
	}

	/* parent process */

#ifdef  USE_UTMP
	if( ( ((ml_pty_unix_t*)pty)->utmp = kik_utmp_new( ml_pty_get_slave_name( pty->slave) ,
						host , pty->master)) == NULL)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG "utmp failed.\n") ;
	#endif
	}
#endif

	pty->child_pid = pid ;
	pty->buf = NULL ;
	pty->left = 0 ;
	pty->size = 0 ;

  	pty->pty_listener = NULL ;

	pty->delete = delete ;
	pty->set_winsize = set_winsize ;
	pty->write = write_to_pty ;
	pty->read = read_pty ;
	
	if( set_winsize( pty , cols , rows) == 0)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG " ml_set_pty_winsize() failed.\n") ;
	#endif
	}

	return  pty ;
}
