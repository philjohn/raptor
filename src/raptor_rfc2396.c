/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * raptor_rfc2396.c - Raptor URI resolving
 *
 * $Id:
 *
 * Copyright (C) 2004 David Beckett - http://purl.org/net/dajobe/
 * Institute for Learning and Research Technology - http://www.ilrt.bris.ac.uk/
 * University of Bristol - http://www.bristol.ac.uk/
 * 
 * This package is Free Software or Open Source available under the
 * following licenses (these are alternatives):
 *   1. GNU Lesser General Public License (LGPL)
 *   2. GNU General Public License (GPL)
 *   3. Mozilla Public License (MPL)
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * full license terms.
 * 
 */


#ifdef HAVE_CONFIG_H
#include <raptor_config.h>
#endif

#ifdef WIN32
#include <win32_raptor_config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* Raptor includes */
#include "raptor.h"
#include "raptor_internal.h"


/**
 * raptor_new_uri_detail - Create a URI detailed structure from a URI string
 * @uri_string: The URI string to split
 * 
 **/
raptor_uri_detail*
raptor_new_uri_detail(const unsigned char *uri_string)
{
  const unsigned char *s = NULL;
  unsigned char *b = NULL;
  raptor_uri_detail *ud;
  size_t uri_len;

  if(!uri_string)
    return NULL;

  uri_len=strlen((const char*)uri_string);

  /* The extra +5 is for the 5 \0s that may be added for each component 
   * even if the entire URI is empty 
   */
  ud=(raptor_uri_detail*)RAPTOR_CALLOC(raptor_uri_detail,
                                       sizeof(raptor_uri_detail)+uri_len+5+1,
                                       1);
  ud->uri_len=uri_len;
  ud->buffer=(unsigned char*)((void*)ud + sizeof(raptor_uri_detail));
  
  s=uri_string;
  b=ud->buffer;


  /* Split the URI into it's syntactic components */

  /* 
   * scheme is checked in more detail since it is important
   * to recognise absolute URIs for resolving, and it is easy to do.
   *
   * scheme = alpha *( alpha | digit | "+" | "-" | "." )
   *    RFC 2396 section 3.1 Scheme Component
   */
  if(*s && isalpha((int)*s)) {
    s++;

    while(*s && (isalnum((int)*s) ||
                 (*s == '+') || (*s == '-') || (*s == '.')))
      s++;
  
    if(*s == ':') {
      /* it matches the URI scheme grammar, so store this as a scheme */
      ud->scheme=b;
      ud->scheme_len=s-uri_string;

      while(*uri_string != ':')
        *b++ = *uri_string++;
      b+= ud->scheme_len;
      
      *b++ = '\0';

      /* and move past the : */
      s++;
    } else
      s=uri_string;
  }


  /* authority */
  if(*s && s[1] && *s == '/' && s[1] == '/') {
    ud->authority=b;

    s+=2; /* skip "//" */
    
    while(*s && *s != '/' && *s != '?' && *s != '#')
      *b++ = *s++;
    
    ud->authority_len=b-ud->authority;
    
    *b++ = '\0';
  }


  /* path */
  if(*s && *s != '?' && *s != '#') {
    ud->path=b;
    
    while(*s && *s != '?' && *s != '#')
      *b++ = *s++;

    ud->path_len=b-ud->path;
    
    *b++ = '\0';
  }


  /* query */
  if(*s && *s == '?') {
    ud->query=b;
    
    s++;
    
    while(*s && *s != '#')
      *b++ = *s++;
    
    ud->query_len=b-ud->query;
    
    *b++ = '\0';
  }

  
  /* fragment identifier - RFC2396 Section 4.1 */
  if(*s && *s == '#') {
    ud->fragment=b;
    
    s++;
    
    while(*s)
      *b++ = *s++;
    
    ud->fragment_len=b-ud->fragment;
    
    *b='\0';
  }

  return ud;
}


void
raptor_free_uri_detail(raptor_uri_detail* uri_detail)
{
  /* Also frees the uri_detail->buffer allocated in raptor_uri_parse() */
  RAPTOR_FREE(raptor_uri_detail, uri_detail);
}


unsigned char*
raptor_uri_detail_to_string(raptor_uri_detail *ud, size_t* len_p)
{
  size_t len=0;
  unsigned char *buffer, *p;
  
  if(ud->scheme)
    len+= ud->scheme_len+1; /* : */
  if(ud->authority)
    len+= 2 + ud->authority_len; /* // */
  if(ud->path)
    len+= ud->path_len;
  if(ud->fragment)
    len+= 1 + ud->fragment_len; /* # */
  if(ud->query)
    len+= 1 + ud->query_len; /* ? */

  if(len_p)
    *len_p=len;
  
  buffer=(unsigned char*)RAPTOR_MALLOC(cstring, len+1);
  if(!buffer)
    return NULL;

  p=buffer;
  
  if(ud->scheme) {
    unsigned char *src=ud->scheme;
    while(*src)
      *p++ = *src++;
    *p++ = ':';
  }
  if(ud->authority) {
    unsigned char *src=ud->authority;
    *p++ = '/';
    *p++ = '/';
    while(*src)
      *p++ = *src++;
  }
  if(ud->path) {
    unsigned char *src=ud->path;
    while(*src)
      *p++ = *src++;
  }
  if(ud->fragment) {
    unsigned char *src=ud->fragment;
    *p++ = '#';
    while(*src)
      *p++ = *src++;
  }
  if(ud->query) {
    unsigned char *src=ud->query;
    *p++ = '?';
    while(*src)
      *p++ = *src++;
  }
  *p='\0';
  
  return buffer;
}


/**
 * raptor_uri_resolve_uri_reference - Resolve a URI to a base URI
 * @base_uri: Base URI string
 * @reference_uri: Reference URI string
 * @buffer: Destination buffer URI
 * @length: Length of destination buffer
 * 
 **/
void
raptor_uri_resolve_uri_reference (const unsigned char *base_uri,
                                  const unsigned char *reference_uri,
                                  unsigned char *buffer, size_t length)
{
  raptor_uri_detail *ref=NULL;
  raptor_uri_detail *base=NULL;
  raptor_uri_detail result; /* static - pointers go to inside ref or base */
  unsigned char *path_buffer=NULL;
  unsigned char *p, *cur, *prev, *s;
  unsigned char last_char;
    
  *buffer = '\0';
  memset(&result, 0, sizeof(result));

  ref=raptor_new_uri_detail(reference_uri);
  if(!ref)
    goto resolve_tidy;
  

  /* is reference URI "" or "#frag"? */
  if(!ref->scheme && !ref->authority && !ref->path && !ref->query) {
    unsigned char c;

    /* Copy base URI to result up to '\0' or '#' */
    for(p=buffer; (c= *base_uri) && c != '#'; p++, base_uri++)
      *p = c;
    *p='\0';
    
    if(ref->fragment) {
      unsigned char *src=ref->fragment;
      /* Append any fragment */
      *p++ = '#';
      while(*src)
        *p++ = *src++;
      *p='\0';
    }
    goto resolve_tidy;
  }
  
  /* reference has a scheme - is an absolute URI */
  if(ref->scheme) {
    strncpy((char*)buffer, (const char*)reference_uri, ref->uri_len+1);
    goto resolve_tidy;
  }
  

  /* now the reference URI must be schemeless, i.e. relative */
  base=raptor_new_uri_detail(base_uri);
  if(!base)
    goto resolve_tidy;

  /* result URI must be of the base URI scheme */
  result.scheme = base->scheme;
  result.scheme_len = base->scheme_len;
  
  /* an authority is given ( [user:pass@]hostname[:port] for http)
   * so the reference URI is like //authority
   */
  if(ref->authority) {
    result.authority = ref->authority;
    result.authority_len = ref->authority_len;
    result.path = ref->path;
    result.path_len = ref->path_len;
    goto resolve_end;
  }

  /* no - so now we have path (maybe with query, fragment) relative to base */
  result.authority = base->authority;
  result.authority_len = base->authority_len;
    

  if(ref->path && *ref->path == '/') {
    /* reference path is absolute, just copy the reference path */
    result.path = ref->path;
    result.path_len = ref->path_len;
    goto resolve_end;
  }


  /* need to resolve relative path */

  /* Build the result path in path_buffer */
  result.path_len=0;

  if(base->path)
    result.path_len += base->path_len;
  else {
    base->path=(unsigned char*)"/"; /* static, but copied and not free()d  */
    result.path_len++;
  }

  if(ref->path)
    result.path_len += ref->path_len;

  /* the resulting path can be no longer than result.path_len */
  path_buffer=(unsigned char*)RAPTOR_MALLOC(cstring, result.path_len+1);
  if(!path_buffer)
    goto resolve_tidy;
  result.path = path_buffer;
  *path_buffer = '\0';
  
  for(p=base->path + base->path_len-1; p > base->path && *p != '/'; p--)
    ;

  if(p >= base->path) {
    result.path_len=p-base->path+1;

    /* Found a /, copy everything before that to path_buffer */
    strncpy((char*)path_buffer, (char*)base->path, result.path_len);
    path_buffer[result.path_len]='\0';
  }

  if(ref->path) {
    strncpy((char*)path_buffer+result.path_len, (const char*)ref->path, ref->path_len+1);
    result.path_len += ref->path_len;
    path_buffer[result.path_len]='\0';
  }


  /* remove all "./" path components */
  for(p=(prev=path_buffer); *p; p++) {
    if(*p != '/')
      continue;

    if(p == (prev+1) && *prev == '.') {
      unsigned char *dest=prev;
      
      p++;
      while(*dest)
        *dest++ = *p++;
      *dest= '\0';
      
      p=prev;
      result.path_len -=2;
    } else {
      prev=p+1;
    }
  }
  
  if(p == (prev+1) && *prev == '.') {
    /* Remove "." at the end of a path */
    *prev = '\0';
    result.path_len--;
  }

  
#if defined(RAPTOR_DEBUG)
  if(result.path_len != strlen(path_buffer))
    RAPTOR_FATAL3("Path length %ld does not match calculated %ld.", (long)strlen(path_buffer), (long)result.path_len);
#endif
    
  /* Remove all "<component>/../" path components */

  /*
   * The pointers:
   *            <component>/../<next>
   *       prev-^       cur-^
   * and p points to the previous prev (can be NULL)
   */
  prev=NULL;
  cur=NULL;
  p=NULL;
  last_char='\0';
  
  for(s=path_buffer; *s; last_char=*s++) {

    /* find the path components */
    if(*s != '/') {
      /* If it is the start or following a /, record a new path component */
      if(!last_char || last_char == '/') {
        /* Store 2 path components */
        if(!prev)
          prev=s;
        else if(!cur)
          cur=s;
      }
      continue;
    }


    /* Wait till there are two path components */
    if(!prev || !cur)
      continue;

#if defined(RAPTOR_DEBUG)
    if(result.path_len != strlen(path_buffer))
      RAPTOR_FATAL3("Path length %ld does not match calculated %ld.", (long)strlen(path_buffer), (long)result.path_len);
#endif
    
    /* If the current one is '..' */
    if(s == (cur+2) && cur[0] == '.' && cur[1] == '.') {
        
      /* and if the previous one isn't '..'
       * (which means it is beyond the root such as a path "/foo/../..")
       */
      if(cur != (prev+3) || prev[0] != '.' || prev[1] != '.') {
        unsigned char *dest=prev;
        
        /* remove the <component>/../<next>
         *       prev-^       cur-^ ^-s 
         */
        size_t len=s-prev+1; /* length of path component we are removing */
        
        s++;
        while(*s)
          *dest++ = *s++;
        *dest = '\0';
        result.path_len -= len;

        if(p && p < prev) {
          /* We know the previous prev path component and we didn't do
           * two adjustments in a row, so can adjust the
           * pointers to continue the newly shortened path:
           * s to the / before <next> (autoincremented by the loop)
           * prev to the previous prev path component
           * cur to NULL. Will be set by the next loop iteration since s
           *   points to a '/', last_char will be set to *s. */
          s=prev-1;
          prev=p;
          cur=NULL;
          p=NULL;
        } else {
          /* Otherwise must start from the beginning again */
          prev=NULL;
          cur=NULL;
          p=NULL;
          last_char='\0';
          s=path_buffer;
        }
        
      }
      
    } else {
      /* otherwise this is not a special path component so 
       * shift the path components stack 
       */
      p=prev;
      prev=cur;
      cur=NULL;
    }

  } 

  
  if(prev && s == (cur+2) && cur[0] == '.' && cur[1] == '.') {
    /* Remove <component>/.. at the end of the path */
    *prev = '\0';
    result.path_len -= (s-prev);
  }


#if defined(RAPTOR_DEBUG)
  if(result.path_len != strlen(path_buffer))
    RAPTOR_FATAL3("Path length %ld does not match calculated %ld.", (long)strlen(path_buffer), (long)result.path_len);
#endif

  resolve_end:
  
  if(ref->query) {
    result.query=ref->query;
    result.query_len=ref->query_len;
  }
  
  if(ref->fragment) {
    result.fragment=ref->fragment;
    result.fragment_len=ref->fragment_len;
  }
  
  p=buffer;
  if(result.scheme) {
    strncpy((char*)p, (const char*)result.scheme, result.scheme_len);
    p += result.scheme_len;
    *p++ = ':';
  }
  
  if(result.authority) {
    *p++ = '/';
    *p++ = '/';
    strncpy((char*)p, (const char*)result.authority, result.authority_len);
    p+= result.authority_len;
  }
  
  if(result.path) {
    strncpy((char*)p, (const char*)result.path, result.path_len);
    p+= result.path_len;
  }
  
  if(result.query) {
    *p++ = '?';
    strncpy((char*)p, (const char*)result.query, result.query_len);
    p+= result.query_len;
  }
  
  if(result.fragment) {
    *p++ = '#';
    strncpy((char*)p, (const char*)result.fragment, result.fragment_len);
    p+= result.fragment_len;
  }
  *p = '\0';
  
  resolve_tidy:
  if(path_buffer)
    RAPTOR_FREE(cstring, path_buffer);
  if(base)
    raptor_free_uri_detail(base);
  if(ref)
    raptor_free_uri_detail(ref);
}




#ifdef STANDALONE

#include <stdio.h>

/* one more prototype */
int main(int argc, char *argv[]);

static const char *program;


static int
check_resolve(const char *base_uri, const char *reference_uri, 
              const char *result_uri)
{
  unsigned char buffer[1024];

  raptor_uri_resolve_uri_reference((const unsigned char*)base_uri,
                                   (const unsigned char*)reference_uri,
                                   buffer, sizeof (buffer));

  if(strcmp((const char*)buffer, result_uri)) {
      fprintf(stderr,
              "%s: raptor_uri_resolve_uri_reference(%s, %s) FAILED giving '%s' != '%s'\n",
              program, base_uri, reference_uri, 
              buffer, result_uri);
      return 1;
  }
#if RAPTOR_DEBUG > 2
  fprintf(stderr,
          "%s: raptor_uri_resolve_uri_reference(%s, %s) OK giving '%s'\n",
          program, base_uri, reference_uri, 
          buffer);
#endif
  return 0;
}


static int
check_parses(const char *uri_string) {
  raptor_uri_detail* ud;
  ud=raptor_new_uri_detail(uri_string);
  if(!ud) {
      fprintf(stderr, "%s: raptor_new_uri_detail(%s) FAILED to parse\n",
              program, uri_string);
      return 1;
  }
#if RAPTOR_DEBUG > 2
  fprintf(stderr, "%s: raptor_new_uri_detail(%s) OK\n",
          program, uri_string);
#endif
  raptor_free_uri_detail(ud);
  return 0;
}


int
main(int argc, char *argv[]) 
{
  const char *base_uri="http://example.org/bpath/cpath/d;p?querystr#frag";
  int failures=0;

  program=raptor_basename(argv[0]);
  
  fprintf(stderr, "%s: Using base URI '%s'\n", program, base_uri);

  /* Tests from RFC2396 Appendix C
   *
   * Modifications:
   *  - add 'path' when items are path components to make easier to read
   *  - use example.org instead of 'a' for the authority
   *  - results are against the base_uri above
   */

  /* Appendix C.1 Normal Examples */
  failures += check_resolve(base_uri, "g:h", "g:h");
  failures += check_resolve(base_uri, "gpath", "http://example.org/bpath/cpath/gpath");
  failures += check_resolve(base_uri, "./gpath", "http://example.org/bpath/cpath/gpath");
  failures += check_resolve(base_uri, "gpath/", "http://example.org/bpath/cpath/gpath/");
  failures += check_resolve(base_uri, "/gpath", "http://example.org/gpath");
  failures += check_resolve(base_uri, "//gpath", "http://gpath");
  failures += check_resolve(base_uri, "?y", "http://example.org/bpath/cpath/?y");
  failures += check_resolve(base_uri, "gpath?y", "http://example.org/bpath/cpath/gpath?y");
  failures += check_resolve(base_uri, "#s", "http://example.org/bpath/cpath/d;p?querystr#s");
  failures += check_resolve(base_uri, "gpath#s", "http://example.org/bpath/cpath/gpath#s");
  failures += check_resolve(base_uri, "gpath?y#s", "http://example.org/bpath/cpath/gpath?y#s");
  failures += check_resolve(base_uri, ";x", "http://example.org/bpath/cpath/;x");
  failures += check_resolve(base_uri, "gpath;x", "http://example.org/bpath/cpath/gpath;x");
  failures += check_resolve(base_uri, "gpath;x?y#s", "http://example.org/bpath/cpath/gpath;x?y#s");
  failures += check_resolve(base_uri, ".", "http://example.org/bpath/cpath/");
  failures += check_resolve(base_uri, "./", "http://example.org/bpath/cpath/");
  failures += check_resolve(base_uri, "..", "http://example.org/bpath/");
  failures += check_resolve(base_uri, "../", "http://example.org/bpath/");
  failures += check_resolve(base_uri, "../gpath", "http://example.org/bpath/gpath");
  failures += check_resolve(base_uri, "../..", "http://example.org/");
  failures += check_resolve(base_uri, "../../", "http://example.org/");
  failures += check_resolve(base_uri, "../../gpath", "http://example.org/gpath");


  /* Appendix C.2 Abnormal Examples */
  failures += check_resolve(base_uri, "", "http://example.org/bpath/cpath/d;p?querystr");

  failures += check_resolve(base_uri, "../../../gpath", "http://example.org/../gpath");
  failures += check_resolve(base_uri, "../../../../gpath", "http://example.org/../../gpath");

  failures += check_resolve(base_uri, "/./gpath", "http://example.org/./gpath");
  failures += check_resolve(base_uri, "/../gpath", "http://example.org/../gpath");
  failures += check_resolve(base_uri, "gpath.", "http://example.org/bpath/cpath/gpath.");
  failures += check_resolve(base_uri, ".gpath", "http://example.org/bpath/cpath/.gpath");
  failures += check_resolve(base_uri, "gpath..", "http://example.org/bpath/cpath/gpath..");
  failures += check_resolve(base_uri, "..gpath", "http://example.org/bpath/cpath/..gpath");

  failures += check_resolve(base_uri, "./../gpath", "http://example.org/bpath/gpath");
  failures += check_resolve(base_uri, "./gpath/.", "http://example.org/bpath/cpath/gpath/");
  failures += check_resolve(base_uri, "gpath/./hpath", "http://example.org/bpath/cpath/gpath/hpath");
  failures += check_resolve(base_uri, "gpath/../hpath", "http://example.org/bpath/cpath/hpath");
  failures += check_resolve(base_uri, "gpath;x=1/./y", "http://example.org/bpath/cpath/gpath;x=1/y");
  failures += check_resolve(base_uri, "gpath;x=1/../y", "http://example.org/bpath/cpath/y");

  failures += check_resolve(base_uri, "gpath?y/./x", "http://example.org/bpath/cpath/gpath?y/./x");
  failures += check_resolve(base_uri, "gpath?y/../x", "http://example.org/bpath/cpath/gpath?y/../x");
  failures += check_resolve(base_uri, "gpath#s/./x", "http://example.org/bpath/cpath/gpath#s/./x");
  failures += check_resolve(base_uri, "gpath#s/../x", "http://example.org/bpath/cpath/gpath#s/../x");

  failures += check_resolve(base_uri, "http:gauthority", "http:gauthority");


  /* Examples from 1.3 */
  failures += check_parses("ftp://ftp.is.co.za/rfc/rfc1808.txt");
  failures += check_parses("gopher://spinaltap.micro.umn.edu/00/Weather/California/Los%20Angeles");
  failures += check_parses("http://www.math.uio.no/faq/compression-faq/part1.html");
  failures += check_parses("mailto:mduerst@ifi.unizh.ch");
  failures += check_parses("news:comp.infosystems.www.servers.unix");
  failures += check_parses("telnet://melvyl.ucop.edu/");
  failures += check_parses("");

  /* This is a not-crashing test */
  raptor_new_uri_detail(NULL);

  /* Extra checks not in RFC2396 */

  /* RDF xml:base check that fragments and query strings are removed */
  failures += check_resolve(base_uri, "gpath/../../../hpath", "http://example.org/hpath");

  /* RDF xml:base check that extra ../ are not lost */
  failures += check_resolve("http://example.org/dir/file", "../../../absfile", "http://example.org/../../absfile");

  /* RDF xml:base check that an absolute URI replaces */
  failures += check_resolve("http://example.org/dir/file", "http://another.example.org/dir2/file2", "http://another.example.org/dir2/file2");


  return failures;
}

#endif
