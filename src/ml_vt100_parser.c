/*
 *	$Id$
 */

#include  "ml_vt100_parser.h"

#include  <string.h>		/* memmove */
#include  <stdlib.h>		/* atoi */
#include  <kiklib/kik_debug.h>
#include  <mkf/mkf_ucs4_map.h>	/* mkf_map_to_ucs4 */
#include  <mkf/mkf_ucs_property.h>
#include  <mkf/mkf_locale_ucs4_map.h>
#include  <mkf/mkf_ko_kr_map.h>

#include  "ml_color.h"
#include  "ml_iscii.h"


#define  CTLKEY_BEL	0x07
#define  CTLKEY_BS	0x08
#define  CTLKEY_TAB	0x09
#define  CTLKEY_LF	0x0a
#define  CTLKEY_VT	0x0b
#define  CTLKEY_CR	0x0d
#define  CTLKEY_SO      0x0e
#define  CTLKEY_SI      0x0f
#define  CTLKEY_ESC	0x1b

#define  CURRENT_STR_P(vt100_parser)  (&vt100_parser->seq[(vt100_parser)->len - (vt100_parser)->left])

#if  0
#define  __DEBUG
#endif

#if  0
#define  ESCSEQ_DEBUG
#endif


/* --- static functions --- */

static size_t
receive_bytes(
	ml_vt100_parser_t *  vt100_parser
	)
{
	size_t  ret ;
	size_t  left ;

	if( vt100_parser->left > 0)
	{
		memmove( vt100_parser->seq , CURRENT_STR_P(vt100_parser) ,
			vt100_parser->left * sizeof( u_char)) ;
	}

	left = PTYMSG_BUFFER_SIZE - vt100_parser->left ;

	if( ( ret = ml_read_pty( vt100_parser->pty ,
		&vt100_parser->seq[vt100_parser->left] , left)) == 0)
	{
		return  0 ;
	}

	vt100_parser->len = ( vt100_parser->left += ret) ;

#ifdef  __DEBUG
	{
		int  counter ;

		kik_debug_printf( KIK_DEBUG_TAG " pty msg (len %d) is received:" , vt100_parser->left) ;

	#if  0
		for( counter = 0 ; counter < vt100_parser->left ; counter ++)
		{
			kik_msg_printf( "%c" , vt100_parser->seq[counter]) ;
		}
		kik_msg_printf( "[END]\n") ;
	#endif

	#if  1
		for( counter = 0 ; counter < vt100_parser->left ; counter ++)
		{
			kik_msg_printf( "[%.2x]" , vt100_parser->seq[counter]) ;
		}
		kik_msg_printf( "[END]\n") ;
	#endif
	}
#endif

	return  1 ;
}

static void
change_font(
	ml_vt100_parser_t *  vt100_parser
	)
{
	if( ( vt100_parser->font = ml_term_screen_get_font( vt100_parser->termscr ,
		vt100_parser->font_attr | vt100_parser->cs)) == NULL)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG
			" no font was found by font attr 0x%x , default USASCII font is used instead.\n" ,
			vt100_parser->font_attr | vt100_parser->cs) ;
	#endif

		if( ( vt100_parser->font = ml_term_screen_get_font( vt100_parser->termscr ,
				vt100_parser->font_attr | US_ASCII)) == NULL &&
			( vt100_parser->font = ml_term_screen_get_font( vt100_parser->termscr ,
				DEFAULT_FONT_ATTR(US_ASCII))) == NULL)
		{
		#ifdef  DEBUG
			kik_warn_printf( KIK_DEBUG_TAG " US-ASCII font not found.\n") ;
			
			abort() ;
		#endif
		}

		vt100_parser->is_usascii_font_for_missing = 1 ;
	}
	else
	{
		vt100_parser->is_usascii_font_for_missing = 0 ;
	}
}

static int
flush_buffer(
	ml_vt100_parser_t *  vt100_parser
	)
{
	ml_char_buffer_t *  buffer ;

	buffer = &vt100_parser->buffer ;

	if( buffer->len == 0)
	{
		return  0 ;
	}
	
#ifdef  __DEBUG
	{
		int  counter ;

		kik_msg_printf( "\nflushing chars(%d)...==>" , buffer->len) ;
		for( counter = 0 ; counter < buffer->len ; counter ++)
		{
			char *  bytes ;

			bytes = ml_char_bytes( &buffer->chars[counter]) ;
			
			if( ml_char_size( &buffer->chars[counter]) == 2)
			{
			#if  0
				kik_msg_printf( "%x%x" , bytes[0] | 0x80 , bytes[1] | 0x80) ;
			#else
				kik_msg_printf( "%c%c" , bytes[0] | 0x80 , bytes[1] | 0x80) ;
			#endif
			}
			else
			{
			#if  0
				kik_msg_printf( "%x" , bytes[0]) ;
			#else
				kik_msg_printf( "%c" , bytes[1]) ;
			#endif
			}
		}

		kik_msg_printf( "<===\n") ;
	}
#endif

	(*buffer->output_func)( vt100_parser->termmdl , buffer->chars , buffer->len) ;

	/* buffer is cleared */
	vt100_parser->buffer.len = 0 ;

#ifdef __DEBUG
	ml_image_dump( vt100_parser->termscr->model->image) ;
#endif

	return  1 ;
}

static void
put_char(
	ml_vt100_parser_t *  vt100_parser ,
	u_char *  ch ,
	size_t  len ,
	mkf_charset_t  cs ,
	mkf_property_t  prop
	)
{
	ml_font_attr_t  width_attr ;
	ml_color_t  fg_color ;
	ml_color_t  bg_color ;
	int  is_comb ;

	if( vt100_parser->buffer.len == PTYMSG_BUFFER_SIZE)
	{
		flush_buffer( vt100_parser) ;
	}

	if( cs == ISO10646_UCS2_1 || cs == ISO10646_UCS4_1)
	{
		/*
		 * checking East Aisan Width property of the char.
		 */
		 
		if( prop & MKF_BIWIDTH)
		{
			width_attr = FONT_BIWIDTH ;
		}
		else if( (prop & MKF_AWIDTH) && vt100_parser->col_size_of_east_asian_width_a == 2)
		{
			width_attr = FONT_BIWIDTH ;
		}
		else
		{
			width_attr = 0 ;
		}
	}
	else
	{
		width_attr = 0 ;
	}

	if( vt100_parser->font == NULL || vt100_parser->cs != cs ||
		(vt100_parser->font_attr & FONT_BIWIDTH) != width_attr)
	{
		vt100_parser->cs = cs ;

		vt100_parser->font_attr &= ~FONT_BIWIDTH ;
		vt100_parser->font_attr |= width_attr ;

		change_font( vt100_parser) ;
	}

	if( vt100_parser->is_usascii_font_for_missing)
	{
		/* using space(0x20) instead */
		
		ch[0] = 0x20 ;
		len = 1 ;
		cs = US_ASCII ;
		prop = 0 ;
	}

	if( vt100_parser->is_reversed)
	{
		fg_color = vt100_parser->bg_color ;
		bg_color = vt100_parser->fg_color ;
	}
	else
	{
		fg_color = vt100_parser->fg_color ;
		bg_color = vt100_parser->bg_color ;
	}

	if( prop & MKF_COMBINING)
	{
		is_comb = 1 ;
	}
	else
	{
		is_comb = 0 ;
	}

	if( ! vt100_parser->termscr->use_dynamic_comb && is_comb)
	{
		if( vt100_parser->buffer.len == 0)
		{
			if( ml_term_model_combine_with_prev_char( vt100_parser->termmdl ,
				ch , len , vt100_parser->font , vt100_parser->font_decor ,
				vt100_parser->fg_color , vt100_parser->bg_color , is_comb))
			{
				return ;
			}
		}
		else
		{
			if( ml_char_combine( &vt100_parser->buffer.chars[vt100_parser->buffer.len - 1] ,
				ch , len , vt100_parser->font , vt100_parser->font_decor ,
				vt100_parser->fg_color , vt100_parser->bg_color , is_comb))
			{
				return ;
			}
		}

		/*
		 * if combining failed , char is normally appended.
		 */
	}

	ml_char_set( &vt100_parser->buffer.chars[vt100_parser->buffer.len++] , ch , len ,
		vt100_parser->font , vt100_parser->font_decor , fg_color , bg_color , is_comb) ;

	if( ! vt100_parser->termscr->use_dynamic_comb &&
		(cs == ISO10646_UCS2_1 || cs == ISO10646_UCS4_1))
	{
		ml_char_t *  prev2 ;
		ml_char_t *  prev ;
		ml_char_t *  cur ;
		int  n ;

		cur = &vt100_parser->buffer.chars[vt100_parser->buffer.len - 1] ;
		n = 0 ;

		if( vt100_parser->buffer.len >= 2)
		{
			prev = cur - 1 ;
		}
		else
		{
			if( ( prev = ml_term_model_get_n_prev_char( vt100_parser->termmdl , ++n)) == NULL)
			{
				return ;
			}
		}
		
		if( vt100_parser->buffer.len >= 3)
		{
			prev2 = cur - 2  ;
		}
		else
		{
			/* possibly NULL */
			prev2 = ml_term_model_get_n_prev_char( vt100_parser->termmdl , ++n) ;
		}
		
		if( ml_is_arabic_combining( prev2 , prev , cur))
		{
			if( vt100_parser->buffer.len >= 2)
			{
				if( ml_char_combine( prev ,
					ch , len , vt100_parser->font , vt100_parser->font_decor ,
					vt100_parser->fg_color , vt100_parser->bg_color , is_comb))
				{
					vt100_parser->buffer.len -- ;
				}
			}
			else
			{
				if( ml_term_model_combine_with_prev_char( vt100_parser->termmdl ,
					ch , len , vt100_parser->font , vt100_parser->font_decor ,
					vt100_parser->fg_color , vt100_parser->bg_color , is_comb))
				{
					vt100_parser->buffer.len -- ;
				}
			}
		}
	}
	
	return ;
}


/*
 * VT100_PARSER Escape Sequence Commands.
 */
 
static void
change_font_attr(
	ml_vt100_parser_t *  vt100_parser ,
	int  flag
	)
{
	ml_font_attr_t  attr ;
	ml_color_t  fg_color ;
	ml_color_t  bg_color ;

	attr = vt100_parser->font_attr ;
	fg_color = vt100_parser->fg_color ;
	bg_color = vt100_parser->bg_color ;

	if( flag == 0)
	{
		/* Normal */
		vt100_parser->font_decor = 0 ;
		attr = DEFAULT_FONT_ATTR(0) ;
		fg_color = ML_FG_COLOR ;
		bg_color = ML_BG_COLOR ;
		vt100_parser->is_reversed = 0 ;
	}
	else if( flag == 1)
	{
		/* Bold */
		RESET_FONT_THICKNESS(attr) ;
		attr |= FONT_BOLD ;
	}
	else if( flag == 4)
	{
		/* Underscore */
		vt100_parser->font_decor |= FONT_UNDERLINE ;
	}
	else if( flag == 5)
	{
		/* Blink */
	}
	else if( flag == 7)
	{
		/* Inverse */
		
		vt100_parser->is_reversed = 1 ;
	}
	else if( flag == 22)
	{
		attr &= ~FONT_BOLD ;
	}
	else if( flag == 24)
	{
		vt100_parser->font_decor &= ~FONT_UNDERLINE ;
	}
	else if( flag == 25)
	{
		/* blink */
	}
	else if( flag == 27)
	{
		vt100_parser->is_reversed = 0 ;
	}
	else if( flag == 30)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "black") ;
	}
	else if( flag == 31)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "red") ;
	}
	else if( flag == 32)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "green") ;
	}
	else if( flag == 33)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "yellow") ;
	}
	else if( flag == 34)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "blue") ;
	}
	else if( flag == 35)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "magenta") ;
	}
	else if( flag == 36)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "cyan") ;
	}
	else if( flag == 37)
	{
		fg_color = ml_term_screen_get_color( vt100_parser->termscr , "white") ;
	}
	else if( flag == 39)
	{
		/* default fg */
		
		fg_color = ML_FG_COLOR ;
		vt100_parser->is_reversed = 0 ;
	}
	else if( flag == 40)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "black") ;
	}
	else if( flag == 41)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "red") ;
	}
	else if( flag == 42)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "green") ;
	}
	else if( flag == 43)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "yellow") ;
	}
	else if( flag == 44)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "blue") ;
	}
	else if( flag == 45)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "magenta") ;
	}
	else if( flag == 46)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "cyan") ;
	}
	else if( flag == 47)
	{
		bg_color = ml_term_screen_get_color( vt100_parser->termscr , "white") ;
	}
	else if( flag == 49)
	{
		bg_color = ML_BG_COLOR ;
		vt100_parser->is_reversed = 0 ;
	}
#ifdef  DEBUG
	else
	{
		kik_warn_printf( KIK_DEBUG_TAG " unknown font attr flag(%d).\n" , flag) ;
	}
#endif
	
	if( attr != vt100_parser->font_attr)
	{
		vt100_parser->font_attr = attr ;
		change_font( vt100_parser) ;
	}

	if( fg_color != ML_UNKNOWN_COLOR && fg_color != vt100_parser->fg_color)
	{
		vt100_parser->fg_color = fg_color ;
	}

	if( bg_color != ML_UNKNOWN_COLOR && bg_color != vt100_parser->bg_color)
	{
		vt100_parser->bg_color = bg_color ;
	}
}

static void
clear_line_all(
	ml_vt100_parser_t *  vt100_parser
	)
{
	ml_term_model_goto_beg_of_line( vt100_parser->termmdl) ;
	ml_term_model_clear_line_to_right( vt100_parser->termmdl) ;
}

static void
clear_display_all(
	ml_vt100_parser_t *  vt100_parser
	)
{
	ml_term_model_goto_home( vt100_parser->termmdl) ;
	ml_term_model_clear_below( vt100_parser->termmdl) ;
}

static int
increment_str(
	u_char **  str ,
	size_t *  left
	)
{
	if( -- (*left) == 0)
	{
		return  0 ;
	}

	(*str) ++ ;

	return  1 ;
}

static int
parse_vt100_escape_sequence(
	ml_vt100_parser_t *  vt100_parser
	)
{
	u_char *  str_p ;
	size_t  left ;

	if( vt100_parser->left == 0)
	{
		/* end of string */
		
		return  1 ;
	}

	str_p = CURRENT_STR_P(vt100_parser) ;
	left = vt100_parser->left ;

	while( 1)
	{
		if( *str_p == CTLKEY_ESC)
		{			
			if( increment_str( &str_p , &left) == 0)
			{
				return  0 ;
			}

		#ifdef  ESCSEQ_DEBUG
			kik_msg_printf( "RECEIVED ESCAPE SEQUENCE: ESC - %c" , *str_p) ;
		#endif

			if( *str_p < 0x20 || 0x7e < *str_p)
			{
			#ifdef  DEBUG
				kik_warn_printf( KIK_DEBUG_TAG " illegal escape sequence ESC - 0x%x.\n" , 
					*str_p) ;
			#endif
			}
			else if( *str_p == '#')
			{
				if( increment_str( &str_p , &left) == 0)
				{
					return  0 ;
				}

				if( *str_p == '8')
				{
					ml_term_screen_fill_all_with_e( vt100_parser->termscr) ;
				}
			}
			else if( *str_p == '7')
			{
				/* save cursor */

				/*
				 * XXX
				 * rxvt-2.7.7 saves following parameters.
				 *  col,row,rstyle,charset,char
				 * (see rxvt_scr_cursor() in screen.c)
				 *
				 * on the other hand , mlterm saves only
				 *   col,row,fg_color,bg_color(aka ml_cursor_t),
				 *   and font_decor,font_attr.
				 * in other words , "char" is not saved , but maybe it works.
				 *
				 * BTW , owing to this behavior , 
				 * "2. Test of screen features" - "Test of the SAVE/RESTORE CURSOR feature"
				 * of vttest fails.
				 */

				ml_term_model_save_cursor( vt100_parser->termmdl) ;
				vt100_parser->saved_decor = vt100_parser->font_decor ;
				vt100_parser->saved_attr = vt100_parser->font_attr ;
			}
			else if( *str_p == '8')
			{
				/* restore cursor */
				
				if( ml_term_model_restore_cursor( vt100_parser->termmdl))
				{
					/* if restore failed , this won't be done. */
					vt100_parser->font_decor = vt100_parser->saved_decor ;
					vt100_parser->font_attr = vt100_parser->saved_attr ;
					vt100_parser->saved_attr = DEFAULT_FONT_ATTR(0) ;
					vt100_parser->saved_decor = 0 ;
				}
			}
			else if( *str_p == '=')
			{
				/* application keypad */

				ml_term_screen_set_app_keypad( vt100_parser->termscr) ;
			}
			else if( *str_p == '>')
			{
				/* normal keypad */

				ml_term_screen_set_normal_keypad( vt100_parser->termscr) ;
			}
			else if( *str_p == 'D')
			{
				/* index(scroll up) */

				ml_term_model_scroll_upward( vt100_parser->termmdl , 1) ;
			}
			else if( *str_p == 'E')
			{
				/* next line */
				
				ml_term_model_line_feed( vt100_parser->termmdl) ;
				ml_term_model_goto_beg_of_line( vt100_parser->termmdl) ;
			}
			else if( *str_p == 'F')
			{
				/* cursor to lower left corner of screen */

			#ifdef  DEBUG
				kik_warn_printf( KIK_DEBUG_TAG
					" cursor to lower left corner is not implemented.\n") ;
			#endif
			}
			else if( *str_p == 'H')
			{
				/* set tab */

				ml_term_model_set_tab_stop( vt100_parser->termmdl) ;
			}
			else if( *str_p == 'M')
			{
				/* reverse index(scroll down) */

				ml_term_model_scroll_downward( vt100_parser->termmdl , 1) ;
			}
			else if( *str_p == 'Z')
			{
				/* return terminal id */

			#ifdef  DEBUG
				kik_warn_printf( KIK_DEBUG_TAG
					" return terminal id is not implemented.\n") ;
			#endif
			}
			else if( *str_p == 'c')
			{
				/* full reset */

				clear_display_all( vt100_parser) ;

				/* XXX  is this necessary ? */
				change_font_attr( vt100_parser , 0) ;
			}
			else if( *str_p == 'l')
			{
				/* memory lock */

			#ifdef  DEBUG
				kik_warn_printf( KIK_DEBUG_TAG " memory lock is not implemented.\n") ;
			#endif
			}
			else if( *str_p == 'm')
			{
				/* memory unlock */

			#ifdef  DEBUG
				kik_warn_printf( KIK_DEBUG_TAG " memory unlock is not implemented.\n") ;
			#endif
			}
			else if( *str_p == '[')
			{
				int  is_dec_priv ;
				int  ps[5] ;
				size_t  num ;

			#ifdef  ESCSEQ_DEBUG
				kik_msg_printf( " - ") ;
			#endif

				if( increment_str( &str_p , &left) == 0)
				{
					return  0 ;
				}

				if( *str_p == '?')
				{
				#ifdef  ESCSEQ_DEBUG
					kik_msg_printf( "%c - " , *str_p) ;
				#endif
				
					is_dec_priv = 1 ;

					if( increment_str( &str_p , &left) == 0)
					{
						return  0 ;
					}
				}
				else
				{
					is_dec_priv = 0 ;
				}

				num = 0 ;
				while( num < 5)
				{
					if( '0' <= *str_p && *str_p <= '9')
					{
						u_char  digit[20] ;
						int  counter ;

						digit[0] = *str_p ;

						for( counter = 1 ; counter < 19 ; counter ++)
						{
							if( increment_str( &str_p , &left) == 0)
							{
								return  0 ;
							}

							if( *str_p < '0' || '9' < *str_p)
							{
								break ;
							}
							
							digit[counter] = *str_p ;
						}

						digit[counter] = '\0' ;

						ps[num ++] = atoi( digit) ;

					#ifdef  ESCSEQ_DEBUG
						kik_msg_printf( "%d - " , ps[num - 1]) ;
					#endif

						if( *str_p != ';')
						{
							/*
							 * "ESC [ 0 n" is regarded as it is.
							 */
							break ;
						}
					}
					else if( *str_p == ';')
					{
						/*
						 * "ESC [ ; n " is regarded as "ESC [ 0 ; n"
						 */
						ps[num ++] = 0 ;
					}
					else
					{
						/*
						 * "ESC [ n" is regarded as "ESC [ 0 n"
						 * => this 0 is ignored after exiting this while block.
						 *
						 * "ESC [ 1 ; n" is regarded as "ESC [ 1 ; 0 n"
						 */
						ps[num ++] = 0 ;

						break ;
					}
					
				#ifdef  ESCSEQ_DEBUG
					kik_msg_printf( "; - ") ;
				#endif
				
					if( increment_str( &str_p , &left) == 0)
					{
						return  0 ;
					}
				}

				/*
				 * XXX
				 * 0 ps of something like ESC [ 0 n is ignored.
				 * if there are multiple ps , no ps is ignored.
				 * adhoc for vttest.
				 */
				if( num == 1 && ps[0] == 0)
				{
					num = 0 ;
				}

			#ifdef  ESCSEQ_DEBUG
				if( *str_p < 0x20 || 0x7e < *str_p)
				{
					kik_msg_printf( "<%x>" , *str_p) ;
				}
				else
				{
					kik_msg_printf( "%c" , *str_p) ;
				}
			#endif

				/*
				 * cursor-control characters inside ESC sequences
				 */
				if( *str_p == 0x8)
				{
					ml_term_model_go_back( vt100_parser->termmdl , 1) ;
					if( increment_str( &str_p , &left) == 0)
					{
						return  0 ;
					}
				}
				
				if( *str_p < 0x20 || 0x7e < *str_p)
				{
				#ifdef  DEBUG 
					kik_warn_printf( KIK_DEBUG_TAG
						" illegal csi sequence ESC - [ - 0x%x.\n" , 
						*str_p) ; 
				#endif
					/*
					 * XXX
					 * hack for screen command (ESC [ 0xfa m).
					 */
					if( increment_str( &str_p , &left) == 0)
					{
						return  0 ;
					}
				}
				else if( is_dec_priv)
				{
					/* DEC private mode */

					if( *str_p == 'h')
					{
						/* DEC Private Mode Set */

						if( ps[0] == 1)
						{
							ml_term_screen_set_app_cursor_keys(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 2)
						{
							/* reset charsets to USASCII */
						}
					#endif
						else if( ps[0] == 3)
						{
							ml_term_screen_resize_columns(
								vt100_parser->termscr , 132) ;
						}
					#if  0
						else if( ps[0] == 4)
						{
							/* smooth scrolling */
						}
					#endif
						else if( ps[0] == 5)
						{
							ml_term_screen_reverse_video(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 6)
						{
							/* relative absolute origins */
						}
					#endif
					#if  0
						else if( ps[0] == 7)
						{
							/* auto wrap */
						}
					#endif
					#if  0
						else if( ps[0] == 8)
						{
							/* auto repeat */
						}
					#endif
					#if  0
						else if( ps[0] == 9)
						{
							/* X10 mouse reporting */
						}
					#endif
						else if( ps[0] == 25)
						{
							ml_term_screen_cursor_visible(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 35)
						{
							/* shift keys */
						}
					#endif
					#if  0
						else if( ps[0] == 40)
						{
							/* 80 <-> 132 */
						}
					#endif
						else if( ps[0] == 47)
						{
							/* Use Alternate Screen Buffer */

							ml_term_model_use_alternative_image(
								vt100_parser->termmdl) ;
						}
					#if  0
						else if( ps[0] == 66)
						{
							/* application key pad */
						}
					#endif
					#if  0
						else if( ps[0] == 67)
						{
							/* have back space */
						}
					#endif
						else if( ps[0] == 1000)
						{
							ml_term_screen_set_mouse_pos_sending(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 1001)
						{
							/* X11 mouse highlighting */
						}
					#endif
					#if  0
						else if( ps[0] == 1010)
						{
							/* scroll to bottom on tty output inhibit */
						}
					#endif
					#if  0
						else if( ps[0] == 1011)
						{
							/* scroll to bottom on key press */
						}
					#endif
					#if  0
						else if( ps[0] == 1047)
						{
							/* secondary screen w/ clearing */
						}
					#endif
					#if  0
						else if( ps[0] == 1048)
						{
							/* alternative cursor save */
						}
					#endif
						else
						{
						#ifdef  DEBUG
							kik_warn_printf( KIK_DEBUG_TAG
								" ESC - [ ? %d h is not implemented.\n" ,
								ps[0]) ;
						#endif
						}
					}
					else if( *str_p == 'l')
					{
						/* DEC Private Mode Reset */

						if( ps[0] == 1)
						{
							ml_term_screen_set_normal_cursor_keys(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 2)
						{
							/* reset charsets to USASCII */
						}
					#endif
						else if( ps[0] == 3)
						{
							ml_term_screen_resize_columns(
								vt100_parser->termscr , 80) ;
						}
					#if  0
						else if( ps[0] == 4)
						{
							/* smooth scrolling */
						}
					#endif
						else if( ps[0] == 5)
						{
							ml_term_screen_restore_video(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 6)
						{
							/* relative absolute origins */
						}
					#endif
					#if  0
						else if( ps[0] == 7)
						{
							/* auto wrap */
						}
					#endif
					#if  0
						else if( ps[0] == 8)
						{
							/* auto repeat */
						}
					#endif
					#if  0
						else if( ps[0] == 9)
						{
							/* X10 mouse reporting */
						}
					#endif
						else if( ps[0] == 25)
						{
							ml_term_screen_cursor_invisible(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 35)
						{
							/* shift keys */
						}
					#endif
					#if  0
						else if( ps[0] == 40)
						{
							/* 80 <-> 132 */
						}
					#endif
						else if( ps[0] == 47)
						{
							/* Use Normal Screen Buffer */
							ml_term_model_use_normal_image(
								vt100_parser->termmdl) ;
						}
					#if  0
						else if( ps[0] == 66)
						{
							/* application key pad */
						}
					#endif
					#if  0
						else if( ps[0] == 67)
						{
							/* have back space */
						}
					#endif
						else if( ps[0] == 1000)
						{
							ml_term_screen_unset_mouse_pos_sending(
								vt100_parser->termscr) ;
						}
					#if  0
						else if( ps[0] == 1001)
						{
							/* X11 mouse highlighting */
						}
					#endif
					#if  0
						else if( ps[0] == 1010)
						{
							/* scroll to bottom on tty output inhibit */
						}
					#endif
					#if  0
						else if( ps[0] == 1011)
						{
							/* scroll to bottom on key press */
						}
					#endif
					#if  0
						else if( ps[0] == 1047)
						{
							/* secondary screen w/ clearing */
						}
					#endif
					#if  0
						else if( ps[0] == 1048)
						{
							/* alternative cursor save */
						}
					#endif
						else
						{
						#ifdef  DEBUG
							kik_warn_printf( KIK_DEBUG_TAG
								" ESC - [ ? %d l is not implemented.\n" ,
								ps[0]) ;
						#endif
						}
					}
					else if( *str_p == 'r')
					{
						/* Restore DEC Private Mode */
					#ifdef  DEBUG
						kik_warn_printf( KIK_DEBUG_TAG
							" ESC - [ ? %d r is not implemented.\n" ,
							ps[0]) ;
					#endif
					}
					else if( *str_p == 's')
					{
						/* Save DEC Private Mode */
					#ifdef  DEBUG
						kik_warn_printf( KIK_DEBUG_TAG
							" ESC - [ ? %d s is not implemented.\n" ,
							ps[0]) ;
					#endif
					}
				#ifdef  DEBUG
					else
					{
						kik_warn_printf( KIK_DEBUG_TAG
							" receiving unknown csi sequence ESC - [ - ? - %c.\n"
							, *str_p) ;
					}
				#endif
				}
				else
				{
					if( *str_p == '@')
					{
						/* insert blank chars */

						if( num == 0)
						{
							ps[0] = 1 ;
						}

						/*
						 * inserting ps[0] blank characters.
						 */
						 
						ml_term_model_insert_blank_chars( vt100_parser->termmdl ,
							ps[0]) ;
					}
					else if( *str_p == 'A' || *str_p == 'e')
					{
						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_upward( vt100_parser->termmdl , ps[0]) ;
					}
					else if( *str_p == 'B')
					{
						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_downward( vt100_parser->termmdl , ps[0]) ;
					}
					else if( *str_p == 'C' || *str_p == 'a')
					{
						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_forward( vt100_parser->termmdl , ps[0]) ;
					}
					else if( *str_p == 'D')
					{
						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_back( vt100_parser->termmdl , ps[0]) ;
					}
					else if( *str_p == 'E')
					{
						/* down and goto first column */

						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_downward( vt100_parser->termmdl , ps[0]) ;
						ml_term_model_goto_beg_of_line( vt100_parser->termmdl) ;
					}
					else if( *str_p == 'F')
					{
						/* up and goto first column */

						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_upward( vt100_parser->termmdl , ps[0]) ;
						ml_term_model_goto_beg_of_line( vt100_parser->termmdl) ;
					}
					else if( *str_p == 'G' || *str_p == '`')
					{
						/* cursor position absolute(CHA or HPA) */
						
						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_horizontally( vt100_parser->termmdl ,
							ps[0] - 1) ;
					}
					else if( *str_p == 'H' || *str_p == 'f')
					{
						if( num == 0)
						{
							ps[0] = 1 ;
							ps[1] = 1 ;
						}
						else
						{
							/*
							 * some applications e.g. vin sometimes use 0 :(
							 */
							if( ps[0] == 0)
							{
								ps[0] = 1 ;
							}

							if( ps[1] == 0)
							{
								ps[1] = 1 ;
							}
						}

						ml_term_model_goto( vt100_parser->termmdl ,
							ps[1] - 1 , ps[0] - 1) ;
					}
					else if( *str_p == 'I')
					{
					#ifdef  DEBUG
						kik_warn_printf( KIK_DEBUG_TAG
							" ESC - [ - I is not implemented.\n") ;
					#endif
					}
					else if( *str_p == 'J')
					{
						/* Erase in Display */

						if( num == 0 || ps[0] == 0)
						{
							ml_term_model_clear_below( vt100_parser->termmdl) ;
						}
						else if( ps[0] == 1)
						{
							ml_term_model_clear_above( vt100_parser->termmdl) ;
						}
						else if( ps[0] == 2)
						{
							clear_display_all( vt100_parser) ;
						}
					}
					else if( *str_p == 'K')
					{
						/* Erase in Line */

						if( num == 0 || ps[0] == 0)
						{
							ml_term_model_clear_line_to_right(
								vt100_parser->termmdl) ;
						}
						else if( ps[0] == 1)
						{
							ml_term_model_clear_line_to_left(
								vt100_parser->termmdl) ;
						}
						else if( ps[0] == 2)
						{
							clear_line_all( vt100_parser) ;
						}
					}
					else if( *str_p == 'L')
					{
						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_insert_new_lines(
							vt100_parser->termmdl , ps[0]) ;
					}
					else if( *str_p == 'M')
					{
						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_delete_lines(
							vt100_parser->termmdl , ps[0]) ;
					}
					else if( *str_p == 'P')
					{
						/* delete chars */

						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_delete_cols( vt100_parser->termmdl , ps[0]) ;
					}
					else if( *str_p == 'S')
					{
					#ifdef  DEBUG
						kik_warn_printf( KIK_DEBUG_TAG
							" ESC - [ - S is not implemented.\n") ;
					#endif
					}
					else if( *str_p == 'T' || *str_p == '^')
					{
						/* initiate hilite mouse tracking. */

					#ifdef  DEBUG
						kik_warn_printf( KIK_DEBUG_TAG
							" ESC - [ - T(^) is not implemented.\n") ;
					#endif
					}
					else if( *str_p == 'X')
					{
					#ifdef  DEBUG
						kik_warn_printf( KIK_DEBUG_TAG
							" ESC - [ - X is not implemented.\n") ;
					#endif
					}
					else if( *str_p == 'c')
					{
						/* send device attributes */

						ml_term_screen_send_device_attr( vt100_parser->termscr) ;
					}
					else if( *str_p == 'd')
					{
						/* line position absolute(VPA) */

						if( num == 0)
						{
							ps[0] = 1 ;
						}

						ml_term_model_go_vertically( vt100_parser->termmdl ,
							ps[0] - 1) ;
					}
					else if( *str_p == 'g')
					{
						/* tab clear */

						if( num == 0)
						{
							ml_term_model_clear_tab_stop(
								vt100_parser->termmdl) ;
						}
						else if( num == 1 && ps[0] == 3)
						{
							ml_term_model_clear_all_tab_stops(
								vt100_parser->termmdl) ;
						}
					}
					else if( *str_p == 'l')
					{
						if( num == 1)
						{
							if( ps[0] == 4)
							{
								/* replace mode */

								vt100_parser->buffer.output_func =
									ml_term_model_overwrite_chars ;
							}
						}
					}
					else if( *str_p == 'h')
					{
						if( num == 1)
						{
							if( ps[0] == 4)
							{
								/* insert mode */

								vt100_parser->buffer.output_func =
									ml_term_model_insert_chars ;
							}
						}
					}
					else if( *str_p == 'm')
					{
						int  counter ;
						
						if( num == 0)
						{
							ps[0] = 0 ;
							num = 1 ;
						}

						for( counter = 0 ; counter < num ; counter ++)
						{
							change_font_attr( vt100_parser , ps[counter]) ;
						}
					}
					else if( *str_p == 'n')
					{
						/* device status report */

						if( num == 1)
						{
							if( ps[0] == 5)
							{
								ml_term_screen_report_device_status(
									vt100_parser->termscr) ;
							}
							else if( ps[0] == 6)
							{
								ml_term_screen_report_cursor_position(
									vt100_parser->termscr) ;
							}
						}
					}
					else if( *str_p == 'r')
					{
						/* set scroll region */
						if( num == 2)
						{
							/*
							 * in case 0 is used.
							 */
							if( ps[0] == 0)
							{
								ps[0] = 1 ;
							}

							if( ps[1] == 0)
							{
								ps[1] = 1 ;
							}

							ml_term_model_set_scroll_region(
								vt100_parser->termmdl ,
								ps[0] - 1 , ps[1] - 1) ;
						}
					}
					else if( *str_p == 'x')
					{
						/* request terminal parameters */

					#ifdef  DEBUG
						kik_warn_printf( KIK_DEBUG_TAG
							" ESC - [ - x is not implemented.\n") ;
					#endif
					}
				#ifdef  DEBUG
					else
					{
						kik_warn_printf( KIK_DEBUG_TAG
							" unknown csi sequence ESC - [ - 0x%x is received.\n" ,
							*str_p , *str_p) ;
					}
				#endif
				}
			}
			else if( *str_p == ']')
			{
				u_char  digit[10] ;
				int  counter ;
				int  ps ;
				u_char *  pt ;

				if( increment_str( &str_p , &left) == 0)
				{
					return  0 ;
				}

				counter = 0 ;
				while( '0' <= *str_p && *str_p <= '9')
				{
					digit[counter++] = *str_p ;

					if( increment_str( &str_p , &left) == 0)
					{
						return  0 ;
					}
				}

				digit[counter] = '\0' ;

				/* if digit is illegal , ps is set 0. */
				ps = atoi( digit) ;

				if( *str_p == ';')
				{
					if( increment_str( &str_p , &left) == 0)
					{
						return  0 ;
					}
					
					pt = str_p ;
					while( *str_p != CTLKEY_BEL)
					{
						if( *str_p == CTLKEY_LF)
						{
							/* stop to parse as escape seq. */
							return  1 ;
						}
						
						if( increment_str( &str_p , &left) == 0)
						{
							return  0 ;
						}
					}

					*str_p = '\0' ;

					if( ps == 0)
					{
						/* change icon name and window title */
						ml_term_screen_set_window_name( vt100_parser->termscr ,
							pt) ;
						ml_term_screen_set_icon_name( vt100_parser->termscr ,
							pt) ;
					}
					else if( ps == 1)
					{
						/* change icon name */
						ml_term_screen_set_icon_name( vt100_parser->termscr ,
							pt) ;
					}
					else if( ps == 2)
					{
						/* change window title */
						ml_term_screen_set_window_name( vt100_parser->termscr ,
							pt) ;
					}
					else if( ps == 20)
					{
						/* image commands */
						char *  p ;
						
						/* XXX discard all adjust./op. settings.*/
						/* XXX may break multi-byte character string. */
						if( ( p = strchr( pt , ';')))
						{
							*p = '\0';
						}
						if( ( p = strchr( pt , ':')))
						{
							*p = '\0';
						}

						if( *pt == '\0')
						{
							/*
							 * Do not change current image but alter
							 * diaplay setting.
							 * XXX nothing can be done for now.
							 */
							 
							return  0 ;
						}

						ml_term_screen_set_config( vt100_parser->termscr ,
							"wall_picture" , pt) ;
					}
					else if( ps == 39)
					{
						ml_term_screen_set_config( vt100_parser->termscr ,
							"fg_color" , pt) ;
					}
					else if( ps == 40)
					{
						ml_term_screen_set_config( vt100_parser->termscr ,
							"bg_color" , pt) ;
					}
					else if( ps == 46)
					{
						/* change log file */
					}
					else if( ps == 50)
					{
						/* set font */
					}
					else if( ps == 5379)
					{
						char *  p ;

						if( ( p = strchr( pt , '=')))
						{
							*(p ++) = '\0' ;
							
							ml_term_screen_set_config( vt100_parser->termscr ,
								pt , p) ;
						}
					}
					else if( ps == 5380)
					{
						ml_term_screen_get_config( vt100_parser->termscr , pt) ;
					}
				}
			}
			else if( *str_p == '(')
			{
				if( IS_ENCODING_BASED_ON_ISO2022(vt100_parser->encoding))
				{
					/* not VT100 control sequence */

					return  1 ;
				}

				if( increment_str( &str_p , &left) == 0)
				{
					return  0 ;
				}

			#ifdef  ESCSEQ_DEBUG
				kik_msg_printf( " - %c" , *str_p) ;
			#endif

				if( *str_p == '0')
				{
					vt100_parser->is_dec_special_in_g0 = 1 ;
				}
				else if( *str_p == 'B')
				{
					vt100_parser->is_dec_special_in_g0 = 0 ;
				}
				else
				{
					/* not VT100 control sequence */

					return  1 ;
				}

				if( ! vt100_parser->is_so)
				{
					vt100_parser->is_dec_special_in_gl =
						vt100_parser->is_dec_special_in_g0 ;
				}
			}
			else if( *str_p == ')')
			{
				if( IS_ENCODING_BASED_ON_ISO2022(vt100_parser->encoding))
				{
					/* not VT100 control sequence */

					return  1 ;
				}

				/*
				 * ignored.
				 */
				 
				if( increment_str( &str_p , &left) == 0)
				{
					return  0 ;
				}
				
			#ifdef  ESCSEQ_DEBUG
				kik_msg_printf( " - %c" , *str_p) ;
			#endif
			
				if( *str_p == '0')
				{
					vt100_parser->is_dec_special_in_g1 = 1 ;
				}
				else if( *str_p == 'B')
				{
					vt100_parser->is_dec_special_in_g1 = 0 ;
				}
				else
				{
					/* not VT100 control sequence */

					return  1 ;
				}
				
				if( vt100_parser->is_so)
				{
					vt100_parser->is_dec_special_in_gl =
						vt100_parser->is_dec_special_in_g1 ;
				}
			}
			else
			{
				/* not VT100 control sequence. */

				return  1 ;
			}

		#ifdef  ESCSEQ_DEBUG
			kik_msg_printf( "\n") ;
		#endif
		}
		else if( *str_p == CTLKEY_SI)
		{
			if( IS_ENCODING_BASED_ON_ISO2022(vt100_parser->encoding))
			{
				/* not VT100 control sequence */
				
				return  1 ;
			}
			
		#ifdef  ESCSEQ_DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " receiving SI\n") ;
		#endif
		
			vt100_parser->is_dec_special_in_gl = vt100_parser->is_dec_special_in_g0 ;
			vt100_parser->is_so = 0 ;
		}
		else if( *str_p == CTLKEY_SO)
		{
			if( IS_ENCODING_BASED_ON_ISO2022(vt100_parser->encoding))
			{
				/* not VT100 control sequence */

				return  1 ;
			}
			
		#ifdef  ESCSEQ_DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " receiving SO\n") ;
		#endif
		
			vt100_parser->is_dec_special_in_gl = vt100_parser->is_dec_special_in_g1 ;
			vt100_parser->is_so = 1 ;
		}
		else if( *str_p == CTLKEY_LF || *str_p == CTLKEY_VT)
		{
		#ifdef  ESCSEQ_DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " receiving LF\n") ;
		#endif

			ml_term_model_line_feed( vt100_parser->termmdl) ;
		}
		else if( *str_p == CTLKEY_CR)
		{
		#ifdef  __DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " receiving CR\n") ;
		#endif

			ml_term_model_goto_beg_of_line( vt100_parser->termmdl) ;
		}
		else if( *str_p == CTLKEY_TAB)
		{
		#ifdef  __DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " receiving TAB\n") ;
		#endif

			ml_term_model_vertical_tab( vt100_parser->termmdl) ;
		}
		else if( *str_p == CTLKEY_BS)
		{
		#ifdef  ESCSEQ_DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " receiving BS\n") ;
		#endif

			ml_term_model_go_back( vt100_parser->termmdl , 1) ;
		}
		else if( *str_p == CTLKEY_BEL)
		{
		#ifdef  ESCSEQ_DEBUG
			kik_debug_printf( KIK_DEBUG_TAG " receiving BEL\n") ;
		#endif

			ml_term_screen_bel( vt100_parser->termscr) ;
		}
		else
		{
			/* not VT100 control sequence */
			
			return  1 ;
		}

		left -- ;
		str_p ++ ;
		
	#ifdef  __DEBUG
		kik_debug_printf( KIK_DEBUG_TAG " --> dumping\n") ;
		ml_image_dump( vt100_parser->termscr->model->image) ;
	#endif

		if( ( vt100_parser->left = left) == 0)
		{
			return  1 ;
		}
	}
}

/*
 * callbacks of pty encoding listener
 */
 
static int
encoding_changed(
	void *  p ,
	ml_char_encoding_t  encoding
	)
{
	ml_vt100_parser_t *  vt100_parser ;
	mkf_parser_t *  cc_parser ;
	mkf_conv_t *  cc_conv ;

	vt100_parser = p ;

	cc_conv = ml_conv_new( encoding) ;
	cc_parser = ml_parser_new( encoding) ;

	if( cc_parser == NULL || cc_conv == NULL)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG " encoding not changed.\n") ;
	#endif
		if( cc_parser)
		{
			(*cc_parser->delete)( cc_parser) ;
		}

		if( cc_conv)
		{
			(*cc_conv->delete)( cc_conv) ;
		}

		return  0 ;
	}
	
#ifdef  DEBUG
	kik_warn_printf( KIK_DEBUG_TAG " encoding changed.\n") ;
#endif

	(*vt100_parser->cc_parser->delete)( vt100_parser->cc_parser) ;
	(*vt100_parser->cc_conv->delete)( vt100_parser->cc_conv) ;

	vt100_parser->encoding = encoding ;
	vt100_parser->cc_parser = cc_parser ;
	vt100_parser->cc_conv = cc_conv ;

	/* reset */
	vt100_parser->is_dec_special_in_gl = 0 ;
	vt100_parser->is_so = 0 ;
	vt100_parser->is_dec_special_in_g0 = 0 ;
	vt100_parser->is_dec_special_in_g1 = 1 ;
	
	return  1 ;
}

static ml_char_encoding_t
pty_encoding(
	void *  p
	)
{
	ml_vt100_parser_t *  vt100_parser ;

	vt100_parser = p ;

	return  vt100_parser->encoding ;
}

static size_t
convert_to_pty_encoding(
	void *  p ,
	u_char *  dst ,
	size_t  len ,
	mkf_parser_t *  parser
	)
{
	ml_vt100_parser_t *  vt100_parser ;

	vt100_parser = p ;

	return  (*vt100_parser->cc_conv->convert)( vt100_parser->cc_conv , dst , len , parser) ;
}

static int
init_pty_encoding(
	void *  p ,
	int  which	/* conv == 0 / parser == 1 */
	)
{
	ml_vt100_parser_t *  vt100_parser ;

	vt100_parser = p ;

	if( which == 1)
	{
		(*vt100_parser->cc_parser->init)( vt100_parser->cc_parser) ;
		vt100_parser->is_dec_special_in_gl = 0 ;
		vt100_parser->is_so = 0 ;
		vt100_parser->is_dec_special_in_g0 = 0 ;
		vt100_parser->is_dec_special_in_g1 = 1 ;
	}
	else if( which == 0)
	{
		(*vt100_parser->cc_conv->init)( vt100_parser->cc_conv) ;
		
		/*
		 * XXX
		 * this causes unexpected behaviors in some applications(e.g. biew) ,
		 * but this is necessary , since 0x00 - 0x7f is not necessarily US-ASCII
		 * in these encodings but key input or selection paste assumes that
		 * 0x00 - 0x7f should be US-ASCII at the initial state.
		 */
		if( IS_STATEFUL_ENCODING(vt100_parser->encoding))
		{
			(*vt100_parser->cc_parser->init)( vt100_parser->cc_parser) ;
			vt100_parser->is_dec_special_in_gl = 0 ;
			vt100_parser->is_so = 0 ;
			vt100_parser->is_dec_special_in_g0 = 0 ;
			vt100_parser->is_dec_special_in_g1 = 1 ;
		}
	}
#ifdef  DEBUG
	else
	{
		kik_warn_printf( KIK_DEBUG_TAG " illegal which value.\n") ;
	}
#endif

	return  1 ;
}


/* --- global functions --- */

ml_vt100_parser_t *
ml_vt100_parser_new(
	ml_term_screen_t *  termscr ,
	ml_term_model_t *  termmdl ,
	ml_char_encoding_t  encoding ,
	int  not_use_unicode_font ,
	int  only_use_unicode_font ,
	u_int  col_size_a
	)
{
	ml_vt100_parser_t *  vt100_parser ;

	if( ( vt100_parser = malloc( sizeof( ml_vt100_parser_t))) == NULL)
	{
		return  NULL ;
	}

	vt100_parser->left = 0 ;
	vt100_parser->len = 0 ;

	ml_str_init( vt100_parser->buffer.chars , PTYMSG_BUFFER_SIZE) ;	
	vt100_parser->buffer.len = 0 ;
	vt100_parser->buffer.output_func = ml_term_model_overwrite_chars ;

	vt100_parser->pty = NULL ;
	
	vt100_parser->termscr = termscr ;
	vt100_parser->termmdl = termmdl ;
	
	vt100_parser->font_attr = DEFAULT_FONT_ATTR(0) ;
	vt100_parser->font_decor = 0 ;
	vt100_parser->saved_attr = DEFAULT_FONT_ATTR(0) ;
	vt100_parser->saved_decor = 0 ;
	vt100_parser->fg_color = ML_FG_COLOR ;
	vt100_parser->bg_color = ML_BG_COLOR ;
	vt100_parser->is_reversed = 0 ;
	vt100_parser->font = NULL ;
	vt100_parser->cs = UNKNOWN_CS ;
	vt100_parser->is_usascii_font_for_missing = 0 ;

	vt100_parser->not_use_unicode_font = not_use_unicode_font ;
	vt100_parser->only_use_unicode_font = only_use_unicode_font ;

	if( ( vt100_parser->cc_conv = ml_conv_new( encoding)) == NULL)
	{
		goto  error ;
	}

	if( ( vt100_parser->cc_parser = ml_parser_new( encoding)) == NULL)
	{
		(*vt100_parser->cc_conv->delete)( vt100_parser->cc_conv) ;

		goto  error ;
	}

	vt100_parser->encoding = encoding ;

	vt100_parser->is_dec_special_in_gl = 0 ;
	vt100_parser->is_so = 0 ;
	vt100_parser->is_dec_special_in_g0 = 0 ;
	vt100_parser->is_dec_special_in_g1 = 1 ;

	if( col_size_a == 1 || col_size_a == 2)
	{
		vt100_parser->col_size_of_east_asian_width_a = col_size_a ;
	}
	else
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG " col size should be 1 or 2. default value 1 is used.\n") ;
	#endif
	
		vt100_parser->col_size_of_east_asian_width_a = 1 ;
	}


	/*
	 * encoding listener
	 */
	 
	vt100_parser->encoding_listener.self = vt100_parser ;
	vt100_parser->encoding_listener.encoding_changed = encoding_changed ;
	vt100_parser->encoding_listener.encoding = pty_encoding ;
	vt100_parser->encoding_listener.convert = convert_to_pty_encoding ;
	vt100_parser->encoding_listener.init = init_pty_encoding ;

	if( ! ml_set_encoding_listener( termscr , &vt100_parser->encoding_listener))
	{
		goto error ;
	}
	
	return  vt100_parser ;

error:
	free( vt100_parser) ;

	return  NULL ;
}

int
ml_vt100_parser_delete(
	ml_vt100_parser_t *  vt100_parser
	)
{
	ml_str_final( vt100_parser->buffer.chars , PTYMSG_BUFFER_SIZE) ;
	(*vt100_parser->cc_parser->delete)( vt100_parser->cc_parser) ;
	(*vt100_parser->cc_conv->delete)( vt100_parser->cc_conv) ;

	free( vt100_parser) ;
	
	return  1 ;
}

int
ml_vt100_parser_set_pty(
	ml_vt100_parser_t *  vt100_parser ,
	ml_pty_t *  pty
	)
{
	if( vt100_parser->pty)
	{
		/* already set */
		
		return  0 ;
	}
	
	vt100_parser->pty = pty ;

	return  1 ;
}

int
ml_parse_vt100_sequence(
	ml_vt100_parser_t *  vt100_parser
	)
{
	mkf_char_t  ch ;
	size_t  prev_left ;

	if( vt100_parser->pty == NULL)
	{
	#ifdef  DEBUG
		kik_warn_printf( KIK_DEBUG_TAG " pty is not set.\n") ;
	#endif
	
		return  0 ;
	}

	if( receive_bytes( vt100_parser))
	{
		ml_term_screen_start_vt100_cmd( vt100_parser->termscr) ;

		/*
		 * bidi and visual-indian is always stopped from here.
		 */
		 
		while( 1)
		{
			prev_left = vt100_parser->left ;

			/*
			 * parsing character encoding.
			 */

			(*vt100_parser->cc_parser->set_str)( vt100_parser->cc_parser ,
				CURRENT_STR_P(vt100_parser) , vt100_parser->left) ;

			while( (*vt100_parser->cc_parser->next_char)( vt100_parser->cc_parser , &ch))
			{
				/*
				 * UCS <-> OTHER CS
				 */ 
				if( ch.cs == ISO10646_UCS4_1)
				{
					if( ch.ch[0] == 0x00 && ch.ch[1] == 0x00 &&
						ch.ch[2] == 0x00 && ch.ch[3] <= 0x7f
						)
					{
						/* this is always done */
						ch.ch[0] = ch.ch[3] ;
						ch.size = 1 ;
						ch.cs = US_ASCII ;
					}
					else if( vt100_parser->not_use_unicode_font)
					{
						/* convert ucs4 to appropriate charset */

						mkf_char_t  non_ucs ;

						if( mkf_map_locale_ucs4_to( &non_ucs , &ch) == 0)
						{
						#ifdef  DEBUG
							kik_warn_printf( KIK_DEBUG_TAG
							" failed to convert ucs4 to other cs.\n") ;
						#endif
							continue ;
						}
						else
						{
							ch = non_ucs ;
						}
					}
				#ifndef  USE_UCS4
					else
					{
						/* change UCS4 to UCS2 */
						ch.ch[0] = ch.ch[2] ;
						ch.ch[1] = ch.ch[3] ;
						ch.size = 2 ;
						ch.cs = ISO10646_UCS2_1 ;
					}
				#endif
				}
				else if( ( vt100_parser->only_use_unicode_font && ch.cs != US_ASCII)
				#if  0
					/* GB18030_2000 2-byte chars(==GBK) are converted to UCS */
					|| ( vt100_parser->encoding == ML_GB18030 && ch.cs == GBK)
				#endif
					/*
					 * XXX
					 * converting japanese gaiji to ucs.
					 */
					|| ch.cs == JISC6226_1978_NEC_EXT
					|| ch.cs == JISC6226_1978_NECIBM_EXT
					|| ch.cs == JISX0208_1983_MAC_EXT
					|| ch.cs == SJIS_IBM_EXT
					)
				{
					mkf_char_t  ucs ;

					if( mkf_map_to_ucs4( &ucs , &ch))
					{
						ucs.property = mkf_get_ucs_property(
								mkf_bytes_to_int( ucs.ch , ucs.size)) ;
						
					#ifdef  USE_UCS4
						ch = ucs ;
					#else
						ch.ch[0] = ucs.ch[2] ;
						ch.ch[1] = ucs.ch[3] ;
						ch.size = 2 ;
						ch.cs = ISO10646_UCS2_1 ;
						ch.property = ucs.property ;
					#endif
					}
				#ifdef  DEBUG
					else
					{
						kik_warn_printf( KIK_DEBUG_TAG
							" mkf_convert_to_ucs4_char() failed.\n") ;
					}
				#endif
				}

				/*
				 * NON UCS <-> NON UCS
				 */
				
				{
					/*
					 * XXX hack
					 * how to deal with johab 10-4-4(8-4-4) font ?
					 * is there any uhc font ?
					 */
					 
					mkf_char_t  uhc ;

					if( ch.cs == JOHAB)
					{
						if( mkf_map_johab_to_uhc( &uhc , &ch) == 0)
						{
							continue ;
						}

						ch = uhc ;
					}

					/*
					 * XXX
					 * switching option whether this conversion is done should
					 * be introduced.
					 */
					if( ch.cs == UHC)
					{
						if( mkf_map_uhc_to_ksc5601_1987( &ch , &uhc) == 0)
						{
							continue ;
						}
					}
				}

				if( ch.size == 1 && ch.ch[0] == 0x0)
				{
				#ifdef  DEBUG
					kik_warn_printf( KIK_DEBUG_TAG
						" 0x0 sequence is received , ignored...\n") ;
				#endif
				}
				else if( ch.size == 1 && 0x1 <= ch.ch[0] && ch.ch[0] <= 0x1f)
				{
					/*
					 * this is a control sequence.
					 * reparsing this char in vt100_escape_sequence() ...
					 */

					vt100_parser->cc_parser->left ++ ;
					vt100_parser->cc_parser->is_eos = 0 ;

					break ;
				}
				else
				{
					vt100_parser->left = vt100_parser->cc_parser->left ;

					if( ml_is_msb_set( ch.cs))
					{
						SET_MSB( ch.ch[0]) ;
					}

					if( ( ch.cs == US_ASCII && vt100_parser->is_dec_special_in_gl) ||
						ch.cs == DEC_SPECIAL)
					{
						if( ch.ch[0] == 0x5f)
						{
							ch.ch[0] = 0x7f ;
						}
						else if( 0x5f < ch.ch[0] && ch.ch[0] < 0x7f)
						{
							ch.ch[0] -= 0x5f ;
						}

						ch.cs = DEC_SPECIAL ;
						ch.property = 0 ;
					}

					put_char( vt100_parser , ch.ch , ch.size , ch.cs ,
						ch.property) ;
				}
			}

			vt100_parser->left = vt100_parser->cc_parser->left ;

			flush_buffer( vt100_parser) ;

			if( vt100_parser->cc_parser->is_eos)
			{
				break ;
			}

			/*
			 * parsing other vt100 sequences.
			 */

			if( ! parse_vt100_escape_sequence( vt100_parser))
			{
				/* shortage of chars */

				break ;
			}

			if( vt100_parser->left == prev_left)
			{
			#ifdef  DEBUG
				kik_debug_printf( KIK_DEBUG_TAG
					" unrecognized sequence[%.2x] is received , ignored...\n" ,
					*CURRENT_STR_P(vt100_parser)) ;
			#endif

				vt100_parser->left -- ;
			}

			if( vt100_parser->left == 0)
			{
				break ;
			}
		}

		ml_term_screen_stop_vt100_cmd( vt100_parser->termscr) ;
	}

	return  1 ;
}
