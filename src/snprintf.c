/*
 * This file is in the Public Domain
 *
 * Based on code from Public Domain snprintf.c from mutt
 *   http://dev.mutt.org/hg/mutt/file/55cd4cb611d9/snprintf.c
 * Tue Aug 08 22:49:12 2006 +0000
 * 
 */

#ifdef HAVE_CONFIG_H
#include <raptor_config.h>
#endif

#ifdef WIN32
#include <win32_raptor_config.h>
#endif

#ifdef HAVE_VASPRINTF
#define _GNU_SOURCE /* to get vasprintf() available */
#endif
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "raptor2.h"
#include "raptor_internal.h"



/* 
 * Thanks to the patch in this Debian bug for the solution
 * to the crash inside vsnprintf on some architectures.
 *
 * "reuse of args inside the while(1) loop is in violation of the
 * specs and only happens to work by accident on other systems."
 *
 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=104325 
 */

#ifndef va_copy
#ifdef __va_copy
#define va_copy(dest, src) __va_copy(dest,src)
#else
#define va_copy(dest, src) (dest) = (src)
#endif
#endif


#ifdef CHECK_VSNPRINTF_RUNTIME
static int vsnprintf_checked = -1;

static int
vsnprintf_check_is_c99(const char *s, ...)
{
  char buffer[32];
  va_list args;
  int r;
  va_start(args, s);
  r = vsnprintf(buffer, 5, s, args);
  va_end(args);

  return (r == 7);
}

static int
vsnprintf_is_c99(void)
{
  if(vsnprintf_checked < 0)
    vsnprintf_checked = vsnprintf_check_is_c99("1234567");
  return vsnprintf_checked;
}
#endif


#define VSNPRINTF_C99_BLOCK(len, buffer, size, format, arguments)      \
  do {                                                                 \
    len = RAPTOR_GOOD_CAST(size_t, vsnprintf(buffer, size, format, arguments)); \
  } while(0)

#define VSNPRINTF_NOT_C99_BLOCK(len, buffer, size, format, arguments)   \
  do {                                                                  \
    if(!buffer || !size) {                                              \
      /* This vsnprintf doesn't return number of bytes required */      \
      size = 2 + strlen(format);                                        \
      len = -1;                                                         \
      while(1) {                                                        \
        va_list args_copy;                                              \
        char* tmp_buffer = RAPTOR_MALLOC(char*, size + 1);              \
                                                                        \
        if(!tmp_buffer)                                                 \
          break;                                                        \
                                                                        \
        /* copy for re-use */                                           \
        va_copy(args_copy, arguments);                                  \
        len = vsnprintf(tmp_buffer, size, format, args_copy);           \
        va_end(args_copy);                                              \
                                                                        \
        /* On windows, vsnprintf() returns -1 if the buffer does not    \
         * fit.  If the buffer exactly fits the string without a NULL   \
         * terminator, it returns the string length and it ends up      \
         * with an unterminated string.  The added check makes sure     \
         * the string returned is terminated - otherwise more buffer    \
         * space is allocated and the while() loop retries.             \
         */                                                             \
        if((len >= 0) && (tmp_buffer[len] == '\0')) {                   \
          len = strlen(tmp_buffer);                                     \
          break;                                                        \
        }                                                               \
        RAPTOR_FREE(char*, tmp_buffer);                                 \
        size += (size >> 1);                                            \
      }                                                                 \
    }                                                                   \
                                                                        \
    if(buffer)                                                          \
      vsnprintf(buffer, len, format, arguments);                        \
  } while(0)

/**
 * raptor_vsnprintf2:
 * @buffer: buffer (or NULL)
 * @size: size of buffer (or 0)
 * @format: printf-style format string
 * @arguments: variable arguments list
 * 
 * Format output for a variable arguments list into an allocated sized buffer.
 *
 * This is a wrapper around system versions of vsnprintf with
 * different call and return conventions.
 *
 * If @buffer is NULL or size is 0 or the buffer size is too small,
 * returns the number of bytes that would be needed for buffer
 * 
 * Return value: number of bytes allocated (excluding NUL) or <0 on failure
 **/
int
raptor_vsnprintf2(char *buffer, size_t size,
                  const char *format, va_list arguments)
{
  size_t len;

  RAPTOR_ASSERT_OBJECT_POINTER_RETURN_VALUE(format, char*, 0);

#ifdef CHECK_VSNPRINTF_RUNTIME

  if(vsnprintf_is_c99())
    VSNPRINTF_C99_BLOCK(len, buffer, size, format, arguments) ;
  else
    VSNPRINTF_NOT_C99_BLOCK(len, buffer, size, format, arguments) ;

#else

#ifdef HAVE_C99_VSNPRINTF
  VSNPRINTF_C99_BLOCK(len, buffer, size, format, arguments) ;
#else
  VSNPRINTF_NOT_C99_BLOCK(len, buffer, size, format, arguments) ;
#endif

#endif
  
  return RAPTOR_BAD_CAST(int, len);
}


/**
 * raptor_vsnprintf:
 * @format: printf-style format string
 * @arguments: variable arguments list
 * 
 * Format output for a variable arguments list into a newly allocated buffer
 *
 * @Deprecated: This does not actually conform to vsnprintf's calling
 * convention and does not return the allocated buffer length.  Use
 * raptor_vsnprintf2() or raptor_vasprintf() instead.
 *
 * Return value: a newly allocated string as the formatted result or NULL on failure
 **/
char*
raptor_vsnprintf(const char *format, va_list arguments) 
{
  int len;
  char *buffer = NULL;

  RAPTOR_ASSERT_OBJECT_POINTER_RETURN_VALUE(format, char*, NULL);

  len = raptor_vasprintf(&buffer, format, arguments);
  if(len < 0)
    return NULL;
  
  return buffer;
}


/**
 * raptor_snprintf:
 * @buffer: buffer (or NULL)
 * @size: bufer size (or 0)
 * @format: printf-style format string
 * @...: format arguments
 * 
 * Format output into an allocated sized buffer
 *
 * This provides a portable version snprintf() over variants on
 * different systems.
 *
 * If @buffer is NULL, calculates the number of bytes needed to
 * allocate for buffer and do no formatting.
 * 
 * Return value: number of bytes allocated (excluding NUL) or 0 on failure
 **/
int
raptor_snprintf(char *buffer, size_t size, const char *format, ...)
{
  va_list arguments;
  int length;

  RAPTOR_ASSERT_OBJECT_POINTER_RETURN_VALUE(format, char*, 0);

  va_start(arguments, format);
  length = raptor_vsnprintf2(buffer, size, format, arguments);
  va_end(arguments);

  return length;
}


/**
 * raptor_vasprintf:
 * @ret: pointer to store buffer
 * @format: printf-style format string
 * @arguments: format arguments list
 * 
 * Format output into a new buffer and return it
 *
 * This is a wrapper around the (GNU) vasprintf function that is not
 * always avaiable.
 * 
 * Return value: number of bytes allocated (excluding NUL) or < 0 on failure
 **/
int
raptor_vasprintf(char **ret, const char *format, va_list arguments)
{
  int length;

  RAPTOR_ASSERT_OBJECT_POINTER_RETURN_VALUE(ret, char**, -1);
  RAPTOR_ASSERT_OBJECT_POINTER_RETURN_VALUE(format, char*, -1);

#ifdef HAVE_VASPRINTF
  length = vasprintf(ret, format, arguments);
#else
  length = raptor_vsnprintf2(NULL, 0, format, arguments);
  if(length < 0) {
    *ret = NULL;
    return length;
  }
  *ret = RAPTOR_MALLOC(char*, length + 1);
  if(!*ret)
    return -1;

  length = raptor_vsnprintf2(*ret, length, format, arguments);
#endif

  return length;
}


static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/**
 * raptor_format_integer:
 * @buffer: buffer (or NULL)
 * @bufsize: size of above (or 0)
 * @integer: integer value to format
 * @base: numeric base up to 36
 * @width: field width (or -1)
 * @padding: padding char (or \0)
 *
 * INTERNAL - Format an integer as a decimal into a buffer or
 * calculate the size needed.
 *
 * Works Like the C99 snprintf() but just for integers.
 *
 * If @buffer is NULL or the @bufsize is too small, the number of
 * bytes needed (excluding NUL) is returned and no formatting is done.
 *
 * Return value: number of bytes needed or written (excluding NUL)
 */
int
raptor_format_integer(char* buffer, size_t bufsize, int integer,
                      unsigned int base, int width, char padding)
{
  int len = 1;
  char *p;
  unsigned int value;

  if(integer < 0) {
    value = (unsigned int)-integer;
    len++;
    width++;
  } else
    value = (unsigned int)integer;
  while(value /= base)
    len++;

  if(width > 0 && width > len)
    len = width;

  if(!buffer || bufsize < RAPTOR_GOOD_CAST(size_t, (len + 1))) /* +1 for NUL */
    return len;

  if(!padding)
    padding = ' ';

  if(integer < 0)
    value = (unsigned int)-integer;
  else
    value = (unsigned int)integer;

  p = &buffer[len];
  *p-- = '\0';
  while(value  > 0 && p >= buffer) {
    *p-- = digits[value % base];
    value /= base;
  }
  while(p >= buffer)
    *p-- = padding;
  if(integer < 0)
    *buffer = '-';

  return len;
}
