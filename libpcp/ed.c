/*
    This file is part of Pretty Curved Privacy (pcp1).

    Copyright (C) 2013 T.Linden.

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

#include "ed.h"

unsigned char * pcp_ed_verify_key(unsigned char *signature, size_t siglen, pcp_pubkey_t *p) {
  unsigned char *message = ucmalloc(siglen - crypto_sign_BYTES);
  unsigned long long mlen;

  if(crypto_sign_open(message, &mlen, signature, siglen, p->masterpub) != 0) {
    fatal("Failed to open the signature using the public key 0x%s!\n", p->id);
    goto errve1;
  }

  return message;

 errve1:
  free(message);
  return NULL;
}

unsigned char * pcp_ed_verify(unsigned char *signature, size_t siglen, pcp_pubkey_t *p) {
  unsigned char *message = ucmalloc(siglen - crypto_sign_BYTES);
  unsigned long long mlen;

  if(crypto_sign_open(message, &mlen, signature, siglen, p->edpub) != 0) {
    fatal("Failed to open the signature using the public key 0x%s!\n", p->id);
    goto errve1;
  }

  return message;

 errve1:
  free(message);
  return NULL;
}

unsigned char *pcp_ed_sign_key(unsigned char *message, size_t messagesize, pcp_key_t *s) {
  unsigned long long mlen = messagesize + crypto_sign_BYTES;
  unsigned char *signature = ucmalloc(mlen);

  crypto_sign(signature, &mlen, message, messagesize, s->mastersecret);

  return signature;
}

unsigned char *pcp_ed_sign(unsigned char *message, size_t messagesize, pcp_key_t *s) {
  unsigned long long mlen = messagesize + crypto_sign_BYTES;
  unsigned char *signature = ucmalloc(mlen);

  crypto_sign(signature, &mlen, message, messagesize, s->edsecret);

  return signature;
}

size_t pcp_ed_sign_buffered(FILE *in, FILE *out, pcp_key_t *s, int z85) {
  unsigned char in_buf[PCP_BLOCK_SIZE];
  size_t cur_bufsize = 0;
  size_t outsize = 0;
  crypto_generichash_state *st = ucmalloc(sizeof(crypto_generichash_state));
  unsigned char hash[crypto_generichash_BYTES_MAX];

  crypto_generichash_init(st, NULL, 0, 0);

  if(z85)
    fprintf(out, "%s\nHash: Blake2\n\n", PCP_SIG_HEADER);

  while(!feof(in)) {
    cur_bufsize = fread(&in_buf, 1, PCP_BLOCK_SIZE, in);
    if(cur_bufsize <= 0)
      break;
    outsize += cur_bufsize;
    crypto_generichash_update(st, in_buf, cur_bufsize);
    fwrite(in_buf, cur_bufsize, 1, out);
  }

  if(ferror(out) != 0) {
    fatal("Failed to write encrypted output!\n");
    free(st);
    return 0;
  }

  crypto_generichash_final(st, hash, crypto_generichash_BYTES_MAX);

  unsigned char *signature = pcp_ed_sign(hash, crypto_generichash_BYTES_MAX, s);
  size_t mlen = + crypto_sign_BYTES + crypto_generichash_BYTES_MAX;

  if(z85) {
    fprintf(out, "\n%s\n Version: PCP v%d.%d.%d\n\n", PCP_SIG_START, PCP_VERSION_MAJOR, PCP_VERSION_MINOR, PCP_VERSION_PATCH);
    size_t zlen;
    char *z85encoded = pcp_z85_encode((unsigned char*)signature, mlen, &zlen);
    fprintf(out, "%s\n%s\n", z85encoded, PCP_SIG_END);
  }
  else {
    fprintf(out, "%s", PCP_SIGPREFIX);
    fwrite(signature, mlen, 1, out);
  }

  if(fileno(in) != 0)
    fclose(in);
  if(fileno(out) != 1)
    fclose(out);

  free(st);

  return outsize;
}

pcp_pubkey_t *pcp_ed_verify_buffered(FILE *in, pcp_pubkey_t *p) {
  unsigned char in_buf[PCP_BLOCK_SIZE/2];
  unsigned char in_next[PCP_BLOCK_SIZE/2];
  unsigned char in_full[PCP_BLOCK_SIZE];

  size_t cur_bufsize = 0;
  size_t next_bufsize = 0;
  size_t full_bufsize = 0;

  int z85 = 0;
  int gotsig = 0;

  unsigned char hash[crypto_generichash_BYTES_MAX];
  char zhead[] = PCP_SIG_HEADER;
  size_t hlen = strlen(PCP_SIG_HEADER);
  size_t hlen2 = 15; /*  hash: blake2\n\n */
  size_t mlen = + crypto_sign_BYTES + crypto_generichash_BYTES_MAX;
  size_t zlen = 262; /*  FIXME: calculate */
  unsigned char z85encoded[zlen];
  unsigned char sighash[mlen];
  char z85sigstart[] = PCP_SIG_START;
  char binsigstart[] = PCP_SIGPREFIX;
  char sigstart[] = PCP_SIG_START;
  size_t siglen, startlen;
  size_t offset = -1;

  crypto_generichash_state *st = ucmalloc(sizeof(crypto_generichash_state));
  crypto_generichash_init(st, NULL, 0, 0);

  /* use two half blocks, to overcome sigs spanning block boundaries */
  cur_bufsize = fread(&in_buf, 1, PCP_BLOCK_SIZE/2, in);

  /*  look for z85 header and cut it out */
  if(_findoffset(in_buf, cur_bufsize, zhead, hlen) == 0) {
    /*  it is armored */
    next_bufsize = cur_bufsize - (hlen+hlen2); /*  size - the header */
    memcpy(in_next, &in_buf[hlen+hlen2], next_bufsize); /*  tmp save */
    memcpy(in_buf, in_next, next_bufsize); /*  put into inbuf without header */
    if(cur_bufsize == PCP_BLOCK_SIZE/2) {
      /*  more to come */
      cur_bufsize = fread(&in_buf[next_bufsize], 1, ((PCP_BLOCK_SIZE/2) - next_bufsize), in);
      cur_bufsize += next_bufsize;
      next_bufsize = 0;
      /*  now we've got the 1st half block in in_buf */
      /*  unless the file was smaller than blocksize/2, */
      /*  in which case it contains all the rest til eof */
    }
    z85 = 1;
  }

  if(z85 == 1) {
    siglen = zlen;
    strcpy(sigstart, z85sigstart);
    startlen = strlen(z85sigstart);
  }
  else {
    siglen = mlen + strlen(binsigstart);
    strcpy(sigstart, binsigstart);
    startlen = strlen(binsigstart);
  }


  while (cur_bufsize > 0) {
    if(cur_bufsize == PCP_BLOCK_SIZE/2) {
      /*  probably not eof */
      next_bufsize = fread(&in_next, 1, PCP_BLOCK_SIZE/2, in);
    }
    else
      next_bufsize = 0; /*  <= this is eof */

    /*  concatenate previous and current buffer */
    if(next_bufsize == 0)
      memcpy(in_full, in_buf, cur_bufsize);
    else {
      memcpy(in_full, in_buf, cur_bufsize);
      memcpy(&in_full[cur_bufsize], in_next, next_bufsize);
    }
    full_bufsize = cur_bufsize+next_bufsize;

    /*  find signature offset */
    offset = _findoffset(in_full, full_bufsize, sigstart, startlen);

    /* printf("offset: %ld, full: %ld, cur: %ld\n", offset, full_bufsize, cur_bufsize); */

    if(offset >= 0 && offset <= PCP_BLOCK_SIZE/2) {
      /*  sig begins within the first half, adjust in_buf size */
      /* printf("1st half\n"); */
      next_bufsize = 0;
      cur_bufsize = offset;
      gotsig = 1;
      if(z85) {
	cur_bufsize -= 1;
	memcpy(z85encoded, &in_full[offset], zlen);
      }
      else
	memcpy(sighash, &in_full[offset + strlen(binsigstart)], mlen);
    }
    else if(full_bufsize - offset == siglen) {
      /*  sig fits within the 2nd half */
      /*  offset: 28279, full: 28413, cur: 16384 */
      /* printf("2nd half\n"); */
      next_bufsize -= siglen;
      gotsig = 1;
      if(z85) {
	cur_bufsize -= 1;
	memcpy(z85encoded, &in_full[full_bufsize - siglen], siglen);
      }
      else
	memcpy(sighash, &in_full[full_bufsize - mlen], mlen);
    }
    else
      offset = 0;

    /*  add previous half block to hash  */
    crypto_generichash_update(st, in_buf, cur_bufsize);
 
    /*  next => in */
    if(next_bufsize > 0) {
      memcpy(in_buf, in_next, next_bufsize);
      cur_bufsize = next_bufsize;
    }
    else
      break;
  } /*  while */

  if(gotsig == 0) {
    fatal("Error, the signature doesn't contain the ed25519 signed hash\n");
    goto errvb1;
  }

  crypto_generichash_final(st, hash, crypto_generichash_BYTES_MAX);

  if(z85) {
    char *z85block = pcp_readz85string(z85encoded, zlen);
    if(z85block == NULL)
      goto errvb1;

    size_t dstlen;
    unsigned char *z85decoded = pcp_z85_decode(z85block, &dstlen);
    if(dstlen != mlen) {
      fatal("z85 decoded signature didn't result in a proper signed hash(got: %ld, expected: %ld)\n", dstlen, mlen);
      goto errvb1;
    }
    memcpy(sighash, z85decoded, mlen);
  }
  /*  else: if unarmored, sighash is already filled */

  /*  huh, how did we made it til here? */
  unsigned char *verifiedhash = NULL;
  if(p == NULL) {
    pcphash_iteratepub(p) {
      verifiedhash = pcp_ed_verify(sighash, mlen, p);
      if(verifiedhash != NULL)
	break;
    }
  }
  else {
    verifiedhash = pcp_ed_verify(sighash, mlen, p);
  }

  if(verifiedhash == NULL)
    goto errvb1;

  if(memcmp(verifiedhash, hash, crypto_generichash_BYTES_MAX) != 0) {
    /*  sig verified, but the hash doesn't */
    fatal("signed hash doesn't match actual hash of signed file content\n");
    free(verifiedhash);
    return NULL;
  }

  return p;
  

 errvb1:
  free(st);
  return NULL;
}

size_t pcp_ed_detachsign_buffered(FILE *in, FILE *out, pcp_key_t *s) {
  unsigned char in_buf[PCP_BLOCK_SIZE];
  size_t cur_bufsize = 0;
  size_t outsize = 0;
  crypto_generichash_state *st = ucmalloc(sizeof(crypto_generichash_state));
  unsigned char hash[crypto_generichash_BYTES_MAX];

  crypto_generichash_init(st, NULL, 0, 0);

  while(!feof(in)) {
    cur_bufsize = fread(&in_buf, 1, PCP_BLOCK_SIZE, in);
    if(cur_bufsize <= 0)
      break;
    outsize += cur_bufsize;
    crypto_generichash_update(st, in_buf, cur_bufsize);
  }

  crypto_generichash_final(st, hash, crypto_generichash_BYTES_MAX);

  unsigned char *signature = pcp_ed_sign(hash, crypto_generichash_BYTES_MAX, s);
  size_t mlen = + crypto_sign_BYTES + crypto_generichash_BYTES_MAX;

  fprintf(out, "\n%s\n Version: PCP v%d.%d.%d\n\n",
	  PCP_SIG_START, PCP_VERSION_MAJOR, PCP_VERSION_MINOR, PCP_VERSION_PATCH);
  size_t zlen;
  char *z85encoded = pcp_z85_encode((unsigned char*)signature, mlen, &zlen);
  fprintf(out, "%s\n%s\n", z85encoded, PCP_SIG_END);

  if(fileno(in) != 0)
    fclose(in);
  if(fileno(out) != 1)
    fclose(out);

  free(st);

  return outsize;
}

pcp_pubkey_t *pcp_ed_detachverify_buffered(FILE *in, FILE *sigfd, pcp_pubkey_t *p) {
  unsigned char in_buf[PCP_BLOCK_SIZE];
  size_t cur_bufsize = 0;
  size_t outsize = 0;
  crypto_generichash_state *st = ucmalloc(sizeof(crypto_generichash_state));
  unsigned char hash[crypto_generichash_BYTES_MAX];
  size_t mlen = + crypto_sign_BYTES + crypto_generichash_BYTES_MAX;

  crypto_generichash_init(st, NULL, 0, 0);

  while(!feof(in)) {
    cur_bufsize = fread(&in_buf, 1, PCP_BLOCK_SIZE, in);
    if(cur_bufsize <= 0)
      break;
    outsize += cur_bufsize;
    crypto_generichash_update(st, in_buf, cur_bufsize);
  }

  crypto_generichash_final(st, hash, crypto_generichash_BYTES_MAX);

  /*  read the sig */
  unsigned char *sig = NULL;
  size_t inputBufSize = 0;
  unsigned char byte[1];
  
  while(!feof(sigfd)) {
    if(!fread(&byte, 1, 1, sigfd))
      break;
    unsigned char *tmp = realloc(sig, inputBufSize + 1);
    sig = tmp;
    memmove(&sig[inputBufSize], byte, 1);
    inputBufSize ++;
  }
  fclose(sigfd);

  if(sig == NULL) {
    fatal("Invalid detached signature\n");
    goto errdea1;
  }


  char *z85block = pcp_readz85string(sig, inputBufSize);
  if(z85block == NULL)
    goto errdea2;

  size_t clen;
  unsigned char *sighash = pcp_z85_decode(z85block, &clen);
  if(sighash == NULL)
    goto errdea3;

  if(clen != mlen) {
    fatal("z85 decoded signature didn't result in a proper signed hash(got: %ld, expected: %ld)\n", clen, mlen);
    goto errdea4;
  }
  
  unsigned char *verifiedhash = NULL;
  if(p == NULL) {
    pcphash_iteratepub(p) {
      verifiedhash = pcp_ed_verify(sighash, mlen, p);
      if(verifiedhash != NULL)
	break;
    }
  }
  else {
    verifiedhash = pcp_ed_verify(sighash, mlen, p);
  }

  if(verifiedhash == NULL)
    goto errdea4;

  if(memcmp(verifiedhash, hash, crypto_generichash_BYTES_MAX) != 0) {
    /*  sig verified, but the hash doesn't */
    fatal("signed hash doesn't match actual hash of signed file content\n");
    goto errdea5;
  }

  free(verifiedhash);
  free(sighash);
  free(z85block);
  free(sig);
  return p;


 errdea5:
  free(verifiedhash);

 errdea4:
  free(sighash);
  
 errdea3:
  free(z85block);
  
 errdea2:
  free(sig);
  
 errdea1:
  return NULL;
}
