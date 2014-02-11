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


#include "keymgmt.h"


char *pcp_getstdin(const char *prompt) {
  char line[255];
  char *out;

  fprintf(stderr, "%s: ", prompt);

  if (fgets(line, 255, stdin) == NULL) {
    fatal("Cannot read from stdin");
    goto errgst;
  }

  line[strcspn(line, "\r\n")] = '\0';

  if ((out = strdup(line)) == NULL) {
    fatal("Cannot allocate memory");
    goto errgst;
  }

  return out;

 errgst:
  return NULL;
}

int pcp_storekey (pcp_key_t *key) {
  if(vault->isnew == 1 || HASH_COUNT(pcpkey_hash) == 0) {
    key->type = PCP_KEY_TYPE_MAINSECRET;
  }

  if(pcpvault_addkey(vault, key, key->type) == 0) {
    if(vault->isnew)
      fprintf(stderr, "new vault created, ");
    fprintf(stderr, "key 0x%s added to %s.\n", key->id, vault->filename);
    return 0;
  }
  
  return 1;
}

void pcp_keygen(char *passwd, char *outfile) {
  pcp_key_t *k = pcpkey_new ();
  pcp_key_t *key = NULL;

  char *owner =  pcp_getstdin("Enter the name of the key owner");
  memcpy(k->owner, owner, strlen(owner) + 1);

  char *mail = pcp_getstdin("Enter the email address of the key owner");
  memcpy(k->mail, _lc(mail), strlen(mail) + 1);

  if(debug)
      pcp_dumpkey(k);

  char *passphrase;
  if(passwd == NULL) {
    pcp_readpass(&passphrase,
		 "Enter passphrase for key encryption",
		 "Enter the passphrase again", 1);
  }
  else {
    passphrase = ucmalloc(strlen(passwd)+1);
    strncpy(passphrase, passwd, strlen(passwd)+1);
  }

  if(strnlen(passphrase, 1024) > 0)
    key = pcpkey_encrypt(k, passphrase);
  else {
    char *yes = pcp_getstdin("WARNING: secret key will be stored unencrypted. Are you sure [yes|NO]?");
    if(strncmp(yes, "yes", 1024) == 0)
      key = k;
    else {
      memset(key, 0, sizeof(pcp_key_t));
      free(key);
      goto errkg1;
    }
  }

  if(key != NULL) {
    fprintf(stderr, "Generated new secret key:\n");
    if(outfile != NULL) {
      pcp_exportsecretkey(key, outfile);
      pcpkey_printshortinfo(key);
      fprintf(stderr, "key stored to file %s, vault unaltered\n", outfile);
      memset(key, 0, sizeof(pcp_key_t));
      free(key);
    }
    else {
      if(pcp_storekey(key) == 0) {
	pcpkey_printshortinfo(key);
	memset(key, 0, sizeof(pcp_key_t));
	free(key);
      }
    }
  }

 errkg1:
  free(mail);
  free(owner);
}


void pcp_listkeys() {
  pcp_key_t *k;

  int nkeys = HASH_COUNT(pcpkey_hash) + HASH_COUNT(pcppubkey_hash);

  if(nkeys > 0) {
    printf("Key ID               Type      Creation Time        Owner\n");

    pcphash_iterate(k) {
      pcpkey_printlineinfo(k);
    }

    pcp_pubkey_t *p;
    pcphash_iteratepub(p) {
      pcppubkey_printlineinfo(p);
    }
  }
  else {
    fatal("The key vault file %s doesn't contain any keys so far.\n", vault->filename);
  }
}


char *pcp_normalize_id(char *keyid) {
  char *id = ucmalloc(17);
  int len = strnlen(keyid, 24);

  if(len == 16) {
    memcpy(id, keyid, 17);
  }
  else if(len < 16) {
    fatal("Specified key id %s is too short!\n", keyid);
    free(id);
    return NULL;
  }
  else if(len > 18) {
    fatal("Specified key id %s is too long!\n", keyid);
    free(id);
    return NULL;
  }
  else {
    if(keyid[0] == '0' && keyid[1] == 'x' && len == 18) {
      int i;
      for(i=0; i<16; ++i) {
	id[i] = keyid[i+2];
      }
      id[16] = 0;
    }
    else {
      fatal("Specified key id %s is too long!\n", keyid);
      free(id);
      return NULL;
    }
  }

  return id;
}

pcp_key_t *pcp_find_primary_secret() {
  pcp_key_t *key = NULL;
  pcp_key_t *k;
  pcphash_iterate(k) {
    if(k->type == PCP_KEY_TYPE_MAINSECRET) {
      key = ucmalloc(sizeof(pcp_key_t));
      memcpy(key, k, sizeof(pcp_key_t));
      return key;
    }
  }

  /*  no primary? whoops */
  int nkeys = HASH_COUNT(pcpkey_hash);
  if(nkeys == 1) {
    pcphash_iterate(k) {
      key = ucmalloc(sizeof(pcp_key_t));
      memcpy(key, k, sizeof(pcp_key_t));
      return key;
    }
  }

  return NULL;
}

void pcp_exportsecret(char *keyid, int useid, char *outfile) {
  pcp_key_t *key = NULL;

  if(useid == 1) {
    /*  look if we've got that one */
    HASH_FIND_STR(pcpkey_hash, keyid, key);
    if(key == NULL) {
      fatal("Could not find a secret key with id 0x%s in vault %s!\n", keyid, vault->filename);
      free(key);
    }
  }
  else {
    /*  look for our primary key */
    key = pcp_find_primary_secret();
    if(key == NULL) {
      fatal("There's no primary secret key in the vault %s!\n", vault->filename);
    }
  }

  if(key != NULL) {
    pcp_exportsecretkey(key, outfile);
  }
}

void pcp_exportsecretkey(pcp_key_t *key, char *outfile) {
  FILE *out;
  if(outfile == NULL) {
    out = stdout;
  }
  else {
    if((out = fopen(outfile, "wb+")) == NULL) {
      fatal("Could not create output file %s", outfile);
      out = NULL;
    }
  }
  
  if(out != NULL) {
    if(debug)
      pcp_dumpkey(key);
    else
      pcpkey_print(key, out);
    /*  scip */
    /* printf("EXPORT:\n"); */
    /*  pcpprint_bin(stdout, key, PCP_RAW_KEYSIZE); printf("\n"); */
  }
}


/*
  if id given, look if it is already a public and export this,
  else we look for a secret key with that id. without a given
  keyid we use the primary key. if no keyid has been given but
  a recipient instead, we try to look up the vault for a match.
 */
void pcp_exportpublic(char *keyid, char *passwd, char *outfile, int format, int armor) {
  FILE *out;
  int is_foreign = 0;
  pcp_pubkey_t *pk = NULL;
  pcp_key_t *sk = NULL;
  Buffer *exported_pk;

  if(outfile == NULL) {
    out = stdout;
  }
  else {
    if((out = fopen(outfile, "wb+")) == NULL) {
      fatal("Could not create output file %s", outfile);
      goto errpcpexpu1;
    }
  }


  if(keyid != NULL) {
    /* keyid specified, check if it exists and if yes, what type it is */
    HASH_FIND_STR(pcppubkey_hash, keyid, pk);
    if(pk == NULL) {
      /* ok, so, then look for a secret key with that id */
      HASH_FIND_STR(pcpkey_hash, keyid, sk);
      if(sk == NULL) {
	fatal("Could not find a key with id 0x%s in vault %s!\n",
		keyid, vault->filename);
	goto errpcpexpu1;
      }
      else {
	/* ok, so it's our own key */
	is_foreign = 0;
      }
    }
    else {
      /* it's a foreign public key, we cannot sign it ourselfes */
      is_foreign = 1;
    }
  }
  else {
    /* we use our primary key anyway */
    sk = pcp_find_primary_secret();
    if(sk == NULL) {
      fatal("There's no primary secret key in the vault %s!\n", vault->filename);
      goto errpcpexpu1;
    }
    is_foreign = 0;
  }


  if(is_foreign == 0) {
    /* decrypt the secret key */
    char *passphrase;
    pcp_readpass(&passphrase,
		 "Enter passphrase to decrypt your secret key", NULL, 1);
    sk = pcpkey_decrypt(sk, passphrase);
    if(sk == NULL) {
      goto errpcpexpu1;
      memset(passphrase, 0, strlen(passphrase));
      free(passphrase);
    }
    memset(passphrase, 0, strlen(passphrase));
    free(passphrase);
  }

  /* now, we're ready for the actual export */
  if(format == EXP_FORMAT_NATIVE) {
    if(is_foreign == 0) {
      exported_pk = pcp_export_rfc_pub(sk);
      if(exported_pk != NULL) {
	if(armor == 1) {
	  size_t zlen;
	  char *z85 = pcp_z85_encode(buffer_get(exported_pk), buffer_size(exported_pk), &zlen);
	  fprintf(out, "%s\r\n%s\r\n%s\r\n", EXP_PK_HEADER, z85, EXP_PK_FOOTER);
	  FILE *t = fopen("binexp", "wb+");
	  fwrite(buffer_get(exported_pk), 1, buffer_size(exported_pk), t);
	  fclose(t);
	  free(z85);
	}
	else
	  fwrite(buffer_get(exported_pk), 1, buffer_size(exported_pk), out);
	buffer_free(exported_pk);
	fprintf(stderr, "public key exported.\n");
      }
    }
    else {
      /* FIXME: export foreign keys unsupported yet */
      fatal("Exporting foreign public keys in native format unsupported yet");
      goto errpcpexpu1;
    }
  }
  else if(format == EXP_FORMAT_PBP) {
    if(is_foreign == 0) {
      exported_pk = pcp_export_pbp_pub(sk);
      if(exported_pk != NULL) {
	/* PBP format requires armoring always */
	size_t zlen;
	char *z85pbp = pcp_z85_encode(buffer_get(exported_pk), buffer_size(exported_pk), &zlen);
	fprintf(out, "%s", z85pbp);
	free(z85pbp);
	buffer_free(exported_pk);
	fprintf(stderr, "public key exported in PBP format.\n");
      }
    }
    else {
      fatal("Exporting foreign public keys in PBP format not possible");
      goto errpcpexpu1;
    }
  }

errpcpexpu1:
  buffer_free(exported_pk);
}



int pcp_importsecret (vault_t *vault, FILE *in) {
  size_t clen;
  char *z85 = pcp_readz85file(in);

  if(z85 == NULL)
    return 1;

  unsigned char *z85decoded = pcp_z85_decode((char *)z85, &clen);
  free(z85);

  if(z85decoded == NULL) {
    fatal("Error: could not decode input - it's probably not Z85.\n");
    return 1;
  }

  if(clen != PCP_RAW_KEYSIZE) {
    fatal("Error: decoded input didn't result to a proper sized key! (got %ld bytes, expected %ld)\n", clen, PCP_RAW_KEYSIZE);
    free(z85decoded);
    return 1;
  }

  /*  all good now, import the blob */
  pcp_key_t *key = ucmalloc(sizeof(pcp_key_t));
  memcpy(key, z85decoded, PCP_RAW_KEYSIZE);
  key2native(key);

  if(debug)
    pcp_dumpkey(key);

  if(pcp_sanitycheck_key(key) == 0) {
    if(key->secret[0] != 0) {
      /*  unencrypted, encrypt it */
      fprintf(stderr, "Key to be imported is unencrypted.\n");
      char *passphrase;
      pcp_readpass(&passphrase, "Enter passphrase for key encryption", NULL, 1);
      key = pcpkey_encrypt(key, passphrase);
    }
    int nkeys = HASH_COUNT(pcpkey_hash);
    if(nkeys == 0)
      key->type = PCP_KEY_TYPE_MAINSECRET;

    if(pcpvault_addkey(vault, (void *)key, PCP_KEY_TYPE_SECRET) == 0) {
      fprintf(stderr, "key 0x%s added to %s.\n", key->id, vault->filename);
      free(key);
      return 0;
    }
  }

  free(key);
  return 1;
}

int pcp_importpublic (vault_t *vault, FILE *in) {
  unsigned char *buf = ucmalloc(2048);
  size_t buflen = fread(buf, 1, 2048, in);
  pcp_keysig_t *sk = NULL;
  pcp_pubkey_t *pub = NULL;

  if(buflen > 0) {
    pcp_ks_bundle_t *bundle = pcp_import_pub(buf, buflen);
    pcp_keysig_t *sk = bundle->s;

    if(bundle != NULL) {
      pcp_pubkey_t *pub = bundle->p;

      if(debug)
	pcp_dumppubkey(pub);

      if(sk == NULL) {
	fatals_ifany();
	char *yes = pcp_getstdin("WARNING: signature doesn't verify, import anyway [yes|NO]?");
	if(strncmp(yes, "yes", 1024) != 0) {
	  free(yes);
	  goto errip1;
	}
	free(yes);
      }

      if(pcp_sanitycheck_pub(pub) == 0) {
	if(pcpvault_addkey(vault, (void *)pub,  PCP_KEY_TYPE_PUBLIC) == 0) {
	  fprintf(stderr, "key 0x%s added to %s.\n", pub->id, vault->filename);
	}
	else
	  goto errip2;

	if(sk != NULL) {
	  if(pcpvault_addkey(vault, sk, sk->type) != 0)
	    goto errip2;
	}
      }
      else
	goto errip2;
    }
  }

 errip2:
  ucfree(pub, sizeof(pcp_pubkey_t));

 errip1:
  if(sk != NULL) {
    ucfree(sk->blob, sk->size);
    ucfree(sk, sizeof(pcp_keysig_t));
  }
  ucfree(buf, 2048);
  return 1;
}

int pcp_importpublicOLD (vault_t *vault, FILE *in) {
  pcp_pubkey_t *pub = NULL;
  int pbpcompat = 0;
  if(pbpcompat == 1) {
    char *date = NULL;
    char *parts = NULL;
    int pnum;
    pbp_pubkey_t *b = ucmalloc(sizeof(pbp_pubkey_t));
    pcp_pubkey_t *tmp = ucmalloc(sizeof(pcp_pubkey_t));
    pub = ucmalloc(sizeof(pcp_pubkey_t));
    unsigned char *buf = ucmalloc(2048);
    unsigned char *bin = ucmalloc(2048);
    size_t buflen;
    size_t klen;

    buflen = fread(buf, 1, 2048, in); /*  base85 encoded */

    /*  remove trailing newline, if any */
    size_t i, nlen;
    nlen = buflen;
    for(i=buflen; i>0; --i) {
      if(buf[i] == '\n' || buf[i] == '\r') {
	buf[i] = '\0';
	nlen -= 1;
      }
    } 

    klen = nlen /5 * 4; /*((nlen + (5 - (nlen % 5))) / 5) * 4;*/

    if(decode_85((char *)bin, (char *)buf, klen) != 0)
      goto errimp1;

    if(klen < sizeof(pbp_pubkey_t) - 1024 - crypto_sign_BYTES) {
      fatal("PBP key seems to be too small, maybe it's not a PBP key (got %ld, expected %ld)\n",
	    klen, sizeof(pbp_pubkey_t) - 1024);
      goto errimp1;
    }

    if(debug)
      _dump("sig", bin, nlen);

    /*  unpad result, if any */
    for(i=klen; i>0; --i) {
      if(bin[i] != '\0' && i < klen) {
	klen = i + 1;
	break;
      }
    }

    /*  use first part as sig and verify */
    memcpy(b, &bin[crypto_sign_BYTES], klen - crypto_sign_BYTES);

    if(debug)
      _dump("sig", bin, klen);

    /* parse the date */
    date = ucmalloc(19);
    memcpy(date, b->iso_ctime, 18);
    date[19] = '\0';
    struct tm c;
    if(strptime(date, "%Y-%m-%dT%H:%M:%S", &c) == NULL) {
      fatal("Failed to parse creation time in PBP public key file (<%s>)\n", date);
      goto errimp2;
    }

    /*  parse the name */
    parts = strtok (b->name, "<>");
    pnum = 0;
    while (parts != NULL) {
      if(pnum == 0)
	memcpy(pub->owner, parts, strlen(parts));
      else if (pnum == 1)
	memcpy(pub->mail, parts, strlen(parts));
      parts = strtok(NULL, "<>");
      pnum++;
    }
    free(parts);

    if(strlen(b->name) == 0) {
      char *owner =  pcp_getstdin("Enter the name of the key owner");
      memcpy(pub->owner, owner, strlen(owner) + 1);
      free(owner);
    }

    /*  fill in the fields */
    pub->ctime = (long)mktime(&c);
    pub->type = PCP_KEY_TYPE_PUBLIC;
    pub->version = PCP_KEY_VERSION;
    pub->serial  = arc4random();
    memcpy(pub->pub, b->pub, crypto_box_PUBLICKEYBYTES);
    memcpy(pub->edpub, b->edpub, crypto_sign_PUBLICKEYBYTES);
    memcpy(pub->id, pcp_getpubkeyid(pub), 17);
    _lc(pub->owner);

    /* edpub used for signing, might differ */
    memcpy(tmp->edpub, b->sigpub, crypto_sign_PUBLICKEYBYTES);

    if(debug) {
      _dump("  mp", tmp->edpub, crypto_sign_PUBLICKEYBYTES);
      _dump("  cp", pub->pub, crypto_sign_PUBLICKEYBYTES);
      _dump("  sp", pub->edpub, crypto_sign_PUBLICKEYBYTES);
      _dump("name", (unsigned char *)pub->owner, strlen(pub->owner));
      _dump(" sig", bin, klen);
      fprintf(stderr, "<%s>\n",  (unsigned char *)pub->owner);
      pcp_dumppubkey(pub);
    }

    unsigned char *sig = pcp_ed_verify(bin, klen, tmp);
    free(tmp);

    if(sig == NULL)
      goto errimp1;

    free(sig);
    free(b);
    free(buf);
    free(bin);
    goto kimp;

  errimp2:
    free(date);

  errimp1:
    free(bin);
    free(pub);
    free(b);
    free(buf);
    return 1;
  }
  else {
    size_t clen;
    char *z85 = pcp_readz85file(in);

    if(z85 == NULL)
      return 1;

    unsigned char *z85decoded = pcp_z85_decode((char *)z85, &clen);
    free(z85);

    if(z85decoded == NULL) {
      fatal("Error: could not decode input - it's probably not Z85 (got %d bytes)\n", clen);
      return 1;
    }

    if(clen != PCP_RAW_PUBKEYSIZE) {
      fatal("Error: decoded input didn't result to a proper sized key (got %d, expected %d)!\n", clen, PCP_RAW_PUBKEYSIZE);
      free(z85decoded);
      return 1;
    }

    /*  all good now */
    pub = ucmalloc(sizeof(pcp_pubkey_t));
    memcpy(pub, z85decoded, PCP_RAW_PUBKEYSIZE);
    pubkey2native(pub);
  }

 kimp:

  if(debug)
    pcp_dumppubkey(pub);
  if(pcp_sanitycheck_pub(pub) == 0) {
    if(pcpvault_addkey(vault, (void *)pub,  PCP_KEY_TYPE_PUBLIC) == 0) {
      fprintf(stderr, "key 0x%s added to %s.\n", pub->id, vault->filename);
      free(pub);
      return 0;
    }
  }

  free(pub);
  return 1;
}

void pcpdelete_key(char *keyid) {
  pcp_pubkey_t *p = pcphash_pubkeyexists(keyid);
  
  if(p != NULL) {
    /*  delete public */
    HASH_DEL(pcppubkey_hash, p);
    free(p);
    vault->unsafed = 1;
    fprintf(stderr, "Public key deleted.\n");
  }
  else {
    pcp_key_t *s = pcphash_keyexists(keyid);
    if(s != NULL) {
      /*  delete secret */
      HASH_DEL(pcpkey_hash, s);
      free(s);
      vault->unsafed = 1;
      fprintf(stderr, "Secret key deleted.\n");
    }
    else {
      fatal("No key with id 0x%s found!\n", keyid);
    }
  }
}

void pcpedit_key(char *keyid) {
  pcp_key_t *key = pcphash_keyexists(keyid);
  
  if(key != NULL) {
    if(key->secret[0] == 0) {
      char *passphrase;
      pcp_readpass(&passphrase, "Enter passphrase to decrypt the key", NULL, 1);
      key = pcpkey_decrypt(key, passphrase);
    }

    if(key != NULL) {
      char *owner =  pcp_getstdin("Enter the name of the key owner");
      memcpy(key->owner, owner, strlen(owner) + 1);

      char *mail = pcp_getstdin("Enter the email address of the key owner");
      memcpy(key->mail, mail, strlen(mail) + 1);

      char *passphrase;
      pcp_readpass(&passphrase, "Enter passphrase for key encryption", NULL, 1);
      key = pcpkey_encrypt(key, passphrase);

      if(key != NULL) {
	if(debug)
	  pcp_dumpkey(key);

	vault->unsafed = 1; /*  will be safed automatically */
	fprintf(stderr, "Key key changed.\n");
      }
    }
  }
  else {
    fatal("No key with id 0x%s found!\n", keyid);
  }
}


char *pcp_find_id_byrec(char *recipient) {
  pcp_pubkey_t *p;
  char *id = NULL;
  _lc(recipient);
  pcphash_iteratepub(p) {
    if(strncmp(p->owner, recipient, 255) == 0) {
      id = ucmalloc(17);
      strncpy(id, p->id, 17);
      break;
    }
    if(strncmp(p->mail, recipient, 255) == 0) {
      id = ucmalloc(17);
      strncpy(id, p->id, 17);
      break;
    }
  }
  return id;
}



