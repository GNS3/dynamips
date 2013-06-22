/*
 * base64.c -- base-64 conversion routines.
 *
 * For license terms, see the file COPYING in this directory.
 *
 * This base 64 encoding is defined in RFC2045 section 6.8,
 * "Base64 Content-Transfer-Encoding", but lines must not be broken in the
 * scheme used here.
 */

#ifndef __BASE64_H__
#define __BASE64_H__

/* Encode into base64 */
void base64_encode(unsigned char *out,const unsigned char *in,int inlen);

/* Decode from base64 */
int base64_decode(unsigned char *out,const unsigned char *in,int maxlen);

#endif
