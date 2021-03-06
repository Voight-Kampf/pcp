/*
    This file is part of Pretty Curved Privacy (pcp1).

    Copyright (C) 2013-2016 T.v.Dein.

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

    You can contact me by mail: <tlinden AT cpan DOT org>.
*/

#include "util.h"

/* lowercase a string */
char *_lc(char *in) {
  size_t len = strlen(in);
  size_t i;
  for(i=0; i<len; ++i)
    in[i] = towlower(in[i]);
  return in;
}

/* find the offset of the beginning of a certain string within binary data */
long int _findoffset(byte *bin, size_t binlen, char *sigstart, size_t hlen) {
  size_t i;
  long int offset = 0;
  int m = 0;

  for(i=0; i<binlen-hlen; ++i) {
    if(memcmp(&bin[i], sigstart, hlen) == 0) {
      offset = i;
      m = 1;
      break;
    }
  }

  if(m == 0)
    offset = -1;


  return offset;
}

/* xor 2 likesized buffers */
void _xorbuf(byte *iv, byte *buf, size_t xlen) {
  size_t i;
  for (i = 0; i < xlen; ++i)
   buf[i] = iv[i] ^ buf[i];
}

/* print some binary data to stderr */
void _dump(char *n, byte *d, size_t s) {
  int l = strlen(n) + 9;
  fprintf(stderr, "%s (%04"FMT_SIZE_T"): ", n, (SIZE_T_CAST)s);
  size_t i;
  int c;
  for (i=0; i<s; ++i) {
    fprintf(stderr, "%02x", d[i]);
    if(i % 36 == 35 && i > 0) {
      fprintf(stderr, "\n");
      for(c=0; c<l; ++c)
        fprintf(stderr, " ");
    }
  }
  fprintf(stderr, "\n");
}

char *_bin2hex(byte *bin, size_t len) {
  char *out = malloc((len*2) + 1);
  size_t i;
  for(i=0; i<len; ++i)
    sprintf(&out[i*2], "%02x", bin[i]);
  out[len*2] = '\0';
  return out;
}

/* via stackoverflow. yes, lazy */
size_t _hex2bin(const char *hex_str, unsigned char *byte_array, size_t byte_array_max) {
    size_t hex_str_len = strlen(hex_str);
    size_t i = 0, j = 0;

    // The output array size is half the hex_str length (rounded up)
    size_t byte_array_size = (hex_str_len+1)/2;

    if (byte_array_size > byte_array_max)
    {
        // Too big for the output array
        return 0;
    }

    if (hex_str_len % 2 == 1)
    {
        // hex_str is an odd length, so assume an implicit "0" prefix
        if (sscanf(&(hex_str[0]), "%1hhx", &(byte_array[0])) != 1)
        {
            return 0;
        }

        i = j = 1;
    }

    for (; i < hex_str_len; i+=2, j++)
    {
        if (sscanf(&(hex_str[i]), "%2hhx", &(byte_array[j])) != 1)
        {
            return 0;
        }
    }

    return byte_array_size;
}



/*
 * Convert byte arrays from big endian  to numbers and vice versa.  Do
 * not  take  care   about  host  endianess.  In   Rob  Pikes'  words:
 * https://commandcenter.blogspot.de/2012/04/byte-order-fallacy.html
 *
 * With this, we  remove all calls to byte swap  functions like b64toh
 * and the  likes. They are problematic  and I really hated  the ifdef
 * mess in platform.h anyway.
 */

uint64_t _wireto64(byte *data) {
  uint64_t i =
    ((uint64_t)data[7]<<0)  |
    ((uint64_t)data[6]<<8)  |
    ((uint64_t)data[5]<<16) |
    ((uint64_t)data[4]<<24) |
    ((uint64_t)data[3]<<32) |
    ((uint64_t)data[2]<<40) |
    ((uint64_t)data[1]<<48) |
    ((uint64_t)data[0]<<56);
  return i;
}

uint32_t _wireto32(byte *data) {
  uint32_t i =
    ((uint32_t)data[3]<<0)  |
    ((uint32_t)data[2]<<8)  |
    ((uint32_t)data[1]<<16) |
    ((uint32_t)data[0]<<24);
  return i;
}

uint16_t _wireto16(byte *data) {
  uint16_t i =
    ((uint16_t)data[1]<<0) |
    ((uint16_t)data[0]<<8);
  return i;
}

void _64towire(uint64_t i, byte *data) {
  data[0] = (i >> 56) & 0xFF;
  data[1] = (i >> 48) & 0xFF;
  data[2] = (i >> 40) & 0xFF;
  data[3] = (i >> 32) & 0xFF;
  data[4] = (i >> 24) & 0xFF;
  data[5] = (i >> 16) & 0xFF;
  data[6] = (i >>  8) & 0xFF;
  data[7] =  i        & 0xFF;
}

void _32towire(uint32_t i, byte *data) {
  data[0] = (i >> 24) & 0xFF;
  data[1] = (i >> 16) & 0xFF;
  data[2] = (i >>  8) & 0xFF;
  data[3] =  i        & 0xFF;
}

void _16towire(uint16_t i, byte *data) {
  data[0] = (i >>  8) & 0xFF;
  data[1] =  i        & 0xFF;
}





/* via https://github.com/chmike/cst_time_memcmp

Licensed as:

The MIT License (MIT)

Copyright (c) 2015 Christophe Meessen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

This is the safest1 variant using subscriptions.

*/
int cst_time_memcmp(const void *m1, const void *m2, size_t n) {
  int res = 0, diff;
  if (m1 != m2 && n && m1 && m2) {
    const unsigned char *pm1 = (const unsigned char *)m1;
    const unsigned char *pm2 = (const unsigned char *)m2;
    do {
      --n;
      diff = pm1[n] - pm2[n];
      res = (res & (((diff - 1) & ~diff) >> 8)) | diff;
    } while (n != 0);
  }
  return ((res - 1) >> 8) + (res >> 8) + 1;
}
