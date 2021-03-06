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


#include "keymgmt.h"


char *pcp_getstdin(const char *prompt) {
  char line[255];
  char *out = NULL;

  fprintf(stderr, "%s: ", prompt);

  if (fgets(line, 255, stdin) == NULL) {
    fatal(ptx, "Cannot read from stdin\n");
    goto errgst;
  }

  line[strcspn(line, "\r\n")] = '\0';

  if ((out = strdup(line)) == NULL) {
    fatal(ptx, "Cannot allocate memory\n");
    goto errgst;
  }

  return out;

 errgst:
  return NULL;
}

int pcp_storekey (pcp_key_t *key) {
  if(vault->isnew == 1 || pcphash_count(ptx) == 0) {
    key->type = PCP_KEY_TYPE_MAINSECRET;
  }

  if(pcpvault_addkey(ptx, vault, key, key->type) == 0) {
    if(vault->isnew)
      fprintf(stderr, "new vault created, ");
    fprintf(stderr, "key 0x%s added to %s.\n", key->id, vault->filename);
    return 0;
  }
  
  return 1;
}

void pcp_keygen(char *passwd) {
  pcp_key_t *k = pcpkey_new ();
  pcp_key_t *key = NULL;

  char *owner =  pcp_getstdin("Enter the name of the key owner");
  if(owner != NULL)
    memcpy(k->owner, owner, strlen(owner) + 1);

  char *mail = pcp_getstdin("Enter the email address of the key owner");
  if(mail != NULL)
    memcpy(k->mail, _lc(mail), strlen(mail) + 1);

  if(debug)
      pcp_dumpkey(k);

  char *passphrase;
  if(passwd == NULL) {
    pcp_readpass(ptx, &passphrase,
                 "Enter passphrase for key encryption",
                 "Enter the passphrase again", 1, NULL);
  }
  else {
    passphrase = passwd;
  }

  if(strnlen(passphrase, 1024) > 0) {
    double ent = pcp_getentropy(passphrase);
    if(ent < 3.32) {
      fprintf(stderr, "WARNING: you are using a weak passphrase (entropy: %lf)!\n", ent);
      char *yes = pcp_getstdin("Are you sure to use it [yes|NO]?");
      if(strncmp(yes, "yes", 1024) != 0) {
        goto errkg1;
      }
    }
    key = pcpkey_encrypt(ptx, k, passphrase);
  }
  else {
    /* No unencrypted secret key allowed anymore [19.08.2015, tom] */
    memset(k, 0, sizeof(pcp_key_t));
    free(k);
    goto errkg1;
  }

  if(key != NULL) {
    fprintf(stderr, "Generated new secret key:\n");
    if(pcp_storekey(key) == 0) {
      pcpkey_printshortinfo(key);
      memset(key, 0, sizeof(pcp_key_t));
      free(key);
    }
  }

  if(passwd == NULL) {
    /* if passwd is set, it'll be free'd in main() */
    sfree(passphrase);
  }
  
 errkg1:
  free(mail);
  free(owner);
}


void pcp_listkeys() {
  pcp_key_t *k;

  int nkeys = pcphash_count(ptx) + pcphash_countpub(ptx);

  if(nkeys > 0) {
    printf("Key ID               Type             Creation Time        Owner\n");

    pcphash_iterate(ptx, k) {
      pcpkey_printlineinfo(k);
    }

    pcp_pubkey_t *p;
    pcphash_iteratepub(ptx, p) {
      pcppubkey_printlineinfo(p);
    }
  }
  else {
    fatal(ptx, "The key vault file %s doesn't contain any keys so far.\n", vault->filename);
  }
}


char *pcp_normalize_id(char *keyid) {
  char *id = ucmalloc(17);
  int len = strnlen(keyid, 24);

  if(len == 16) {
    memcpy(id, keyid, 17);
  }
  else if(len < 16) {
    fatal(ptx, "Specified key id %s is too short!\n", keyid);
    free(id);
    return NULL;
  }
  else if(len > 18) {
    fatal(ptx, "Specified key id %s is too long!\n", keyid);
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
      fatal(ptx, "Specified key id %s is too long!\n", keyid);
      free(id);
      return NULL;
    }
  }

  return id;
}

pcp_key_t *pcp_find_primary_secret() {
  pcp_key_t *k;
  pcphash_iterate(ptx, k) {
    if(k->type == PCP_KEY_TYPE_MAINSECRET) {
      return k;
    }
  }

  /*  no primary? whoops */
  int nkeys = pcphash_count(ptx);
  if(nkeys == 1) {
    pcphash_iterate(ptx, k) {
      return k;
    }
  }

  return NULL;
}

void pcp_exportsecret(char *keyid, int useid, char *outfile, int armor, char *passwd) {
  pcp_key_t *key = NULL;

  if(useid == 1) {
    /*  look if we've got that one */
    key = pcphash_keyexists(ptx, keyid);
    if(key == NULL) {
      fatal(ptx, "Could not find a secret key with id 0x%s in vault %s!\n", keyid, vault->filename);
      goto errexpse1;
    }
  }
  else {
    /*  look for our primary key */
    key = pcp_find_primary_secret();
    if(key == NULL) {
      fatal(ptx, "There's no primary secret key in the vault %s!\n", vault->filename);
      goto errexpse1;
    }
  }

  FILE *out;
  if(outfile == NULL) {
    out = stdout;
  }
  else {
    if((out = fopen(outfile, "wb+")) == NULL) {
      fatal(ptx, "Could not create output file %s\n", outfile);
       goto errexpse1;
    }
  }
  
  if(out != NULL) {
    if(debug)
      pcp_dumpkey(key);

    if(passwd == NULL) {
      char *passphrase;
      pcp_readpass(ptx, &passphrase,
                   "Enter passphrase to decrypt your secret key", NULL, 1, NULL);
      key = pcpkey_decrypt(ptx, key, passphrase);
      if(key == NULL) {
        sfree(passphrase);
        goto errexpse1;
      }
      sfree(passphrase);
    }
    else {
      key = pcpkey_decrypt(ptx, key, passwd);
      if(key == NULL) {
        goto errexpse1;
      }
    }

    Buffer *exported_sk;

    if(passwd != NULL) {
      exported_sk = pcp_export_secret(ptx, key, passwd);
    }
    else {
      char *passphrase;
      pcp_readpass(ptx, &passphrase,
                  "Enter passphrase to encrypt the exported secret key",
                   "Repeat passphrase", 1, NULL);
      exported_sk = pcp_export_secret(ptx, key, passphrase);
      sfree(passphrase);
    }

    if(exported_sk != NULL) {
      if(armor == 1) {
        size_t zlen;
        char *z85 = pcp_z85_encode(buffer_get(exported_sk), buffer_size(exported_sk), &zlen, 1);
        fprintf(out, "%s\r\n%s\r\n%s\r\n", EXP_SK_HEADER, z85, EXP_SK_FOOTER);
        free(z85);
      }
      else {
        fwrite(buffer_get(exported_sk), 1, buffer_size(exported_sk), out);
      }
      buffer_free(exported_sk);
      fprintf(stderr, "secret key exported.\n");
    }

  }

  errexpse1:
  ;
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
  Buffer *exported_pk = NULL;

  if(outfile == NULL) {
    out = stdout;
  }
  else {
    if((out = fopen(outfile, "wb+")) == NULL) {
      fatal(ptx, "Could not create output file %s\n", outfile);
      goto errpcpexpu1;
    }
  }

  if(keyid != NULL) {
    /* keyid specified, check if it exists and if yes, what type it is */
    pk = pcphash_pubkeyexists(ptx, keyid);
    if(pk == NULL) {
      /* ok, so, then look for a secret key with that id */
      sk = pcphash_keyexists(ptx, keyid);
      if(sk == NULL) {
        fatal(ptx, "Could not find a key with id 0x%s in vault %s!\n",
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
      fatal(ptx, "There's no primary secret key in the vault %s!\n", vault->filename);
      goto errpcpexpu1;
    }
    is_foreign = 0;
  }


  if(is_foreign == 0 && sk->secret[0] == 0 && format <=  EXP_FORMAT_PBP) {
    /* decrypt the secret key */
    if(passwd != NULL) {
      sk = pcpkey_decrypt(ptx, sk, passwd);
    }
    else {
      char *passphrase;
      pcp_readpass(ptx, &passphrase,
                   "Enter passphrase to decrypt your secret key", NULL, 1, NULL);
      sk = pcpkey_decrypt(ptx, sk, passphrase);
      sfree(passphrase);
    }
    if(sk == NULL) {
      goto errpcpexpu1;
    }
  }

  /* now, we're ready for the actual export */
  if(format == EXP_FORMAT_NATIVE) {
    if(is_foreign == 0) {
      exported_pk = pcp_export_rfc_pub(ptx, sk);
      if(exported_pk != NULL) {
        if(armor == 1) {
          size_t zlen;
          char *z85 = pcp_z85_encode(buffer_get(exported_pk), buffer_size(exported_pk), &zlen, 1);
          fprintf(out, "%s\r\n%s\r\n%s\r\n", EXP_PK_HEADER, z85, EXP_PK_FOOTER);
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
      fatal(ptx, "Exporting foreign public keys in native format unsupported yet\n");
      goto errpcpexpu1;
    }
  }
  else if(format == EXP_FORMAT_PBP) {
    if(is_foreign == 0) {
      exported_pk = pcp_export_pbp_pub(sk);
      if(exported_pk != NULL) {
        /* PBP format requires armoring always */
        size_t zlen;
        char *z85pbp = pcp_z85_encode(buffer_get(exported_pk), buffer_size(exported_pk), &zlen, 1);
        fprintf(out, "%s", z85pbp);
        free(z85pbp);
        buffer_free(exported_pk);
        fprintf(stderr, "public key exported in PBP format.\n");
      }
    }
    else {
      fatal(ptx, "Exporting foreign public keys in PBP format not possible\n");
      goto errpcpexpu1;
    }
  }

 errpcpexpu1:
  ;
}



void pcpdelete_key(char *keyid) {
  pcp_pubkey_t *p = pcphash_pubkeyexists(ptx, keyid);
  
  if(p != NULL) {
    /*  delete public */
    pcp_keysig_t *sig = pcphash_keysigexists(ptx, keyid);
    if(sig != NULL) {
      /* also delete associted sig, if any */
      pcphash_del(ptx, sig, sig->type);
    }
    pcphash_del(ptx, p, p->type);
    vault->unsafed = 1;
    fprintf(stderr, "Public key deleted.\n");
  }
  else {
    pcp_key_t *s = pcphash_keyexists(ptx, keyid);
    if(s != NULL) {
      /*  delete secret */
      pcphash_del(ptx, s, s->type);
      vault->unsafed = 1;
      fprintf(stderr, "Secret key deleted.\n");
    }
    else {
      fatal(ptx, "No key with id 0x%s found!\n", keyid);
    }
  }
}

void pcpedit_key(char *keyid) {
  pcp_key_t *key = pcphash_keyexists(ptx, keyid);

  if(key != NULL) {
    if(key->secret[0] == 0) {
      char *passphrase;
      pcp_readpass(ptx, &passphrase, "Enter passphrase to decrypt the key", NULL, 1, NULL);
      key = pcpkey_decrypt(ptx, key, passphrase);
      sfree(passphrase);
    }

    if(key != NULL) {
      fprintf(stderr, "Current owner: %s\n", key->owner);
      char *owner =  pcp_getstdin("  enter new name or press enter to keep current");
      if(strlen(owner) > 0)
        memcpy(key->owner, owner, strlen(owner) + 1);

      fprintf(stderr, "Current mail: %s\n", key->mail);
      char *mail =  pcp_getstdin("  enter new email or press enter to keep current");
      if(strlen(mail) > 0)
        memcpy(key->mail, mail, strlen(mail) + 1);

      free(owner);
      free(mail);

      if(key->type != PCP_KEY_TYPE_MAINSECRET) {
        pcp_key_t *other = NULL;
        uint8_t haveprimary = 0;
        pcphash_iterate(ptx, other) {
          if(other->type == PCP_KEY_TYPE_MAINSECRET) {
            haveprimary = 1;
            break;
          }
        }

        char *yes = NULL;
        if(! haveprimary) {
          fprintf(stderr, "There is currently no primary secret in your vault,\n");
          yes = pcp_getstdin("want to make this one the primary [yes|NO]?");
        }
        else {
          fprintf(stderr, "The key %s is currently the primary secret,\n", other->id);
          yes = pcp_getstdin("want to make this one the primary instead [yes|NO]?");
        }

        if(strncmp(yes, "yes", 1024) == 0) {
          key->type = PCP_KEY_TYPE_MAINSECRET;
          if(haveprimary) {
            fprintf(stderr, "other type: %d\n", other->type);
            other->type = PCP_KEY_TYPE_SECRET;
            fprintf(stderr, "  new type: %d\n", other->type);
          }
        }
        free(yes);
      }

      char *passphrase;
      pcp_readpass(ptx, &passphrase,
                   "Enter new passphrase for key encryption (press enter to keep current)",
                   "Enter the passphrase again", 1, NULL);

      if(strnlen(passphrase, 1024) > 0) {
        key = pcpkey_encrypt(ptx, key, passphrase);
        sfree(passphrase);
      }

      if(key != NULL) {
        if(debug)
          pcp_dumpkey(key);

        vault->unsafed = 1; /*  will be safed automatically */
        fprintf(stderr, "Key %s changed.\n", key->id);
      }
    }
  }
  else {
    fatal(ptx, "No key with id 0x%s found!\n", keyid);
  }
}


char *pcp_find_id_byrec(char *recipient) {
  pcp_pubkey_t *p;
  char *id = NULL;
  _lc(recipient);
  pcphash_iteratepub(ptx, p) {
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


int pcp_import (vault_t *vault, FILE *in, char *passwd) {
  byte *buf = ucmalloc(PCP_BLOCK_SIZE);
  size_t bufsize;
  pcp_pubkey_t *pub = NULL;
  pcp_key_t *sk = NULL;
  pcp_ks_bundle_t *bundle = NULL;
  pcp_keysig_t *keysig = NULL;
  int success = 1; /* default fail */

  Pcpstream *pin = ps_new_file(in);
  ps_setdetermine(pin, 1024);

  bufsize = ps_read(pin, buf, PCP_BLOCK_SIZE);

  if(bufsize == 0) {
    fatal(ptx, "Input file is empty!\n");
    goto errimp1;
  }

  /* first try as rfc pub key */
  bundle = pcp_import_binpub(ptx, buf, bufsize);
  if(bundle != NULL) {
    keysig = bundle->s;
    pub = bundle->p;

    if(debug)
      pcp_dumppubkey(pub);

    if(keysig == NULL) {
      fatals_ifany(ptx);
      char *yes = pcp_getstdin("WARNING: signature doesn't verify, import anyway [yes|NO]?");
      if(strncmp(yes, "yes", 1024) != 0) {
        free(yes);
        goto errimp2;
      }
      free(yes);
    }

    if(pcp_sanitycheck_pub(ptx, pub) == 0) {
      if(pcpvault_addkey(ptx, vault, (void *)pub,  PCP_KEY_TYPE_PUBLIC) == 0) {
        fprintf(stderr, "key 0x%s added to %s.\n", pub->id, vault->filename);
        /* avoid double free */
        success = 0;
      }
      else
        goto errimp2;
      
      if(keysig != NULL) {
        if(pcpvault_addkey(ptx, vault, keysig, keysig->type) != 0) {
          /* FIXME: remove pubkey if storing the keysig failed */
          goto errimp2;
        }
      }
    }
    else
      goto errimp2;
  }
  else {
    /* it's not public key, so let's try to interpret it as secret key */
    if(ptx->verbose)
      fatals_ifany(ptx);
    if(passwd != NULL) {
      sk = pcp_import_secret(ptx, buf, bufsize, passwd);
    }
    else {
      char *passphrase;
      pcp_readpass(ptx, &passphrase,
                   "Enter passphrase to decrypt the secret key file", NULL, 1, NULL);
      sk = pcp_import_secret(ptx, buf, bufsize, passphrase);
      sfree(passphrase);
    }

    if(sk == NULL) {
      goto errimp2;
    }

    if(debug)
      pcp_dumpkey(sk);

    pcp_key_t *maybe = pcphash_keyexists(ptx, sk->id);
    if(maybe != NULL) {
      fatal(ptx, "Secretkey sanity check: there already exists a key with the id 0x%s\n", sk->id);
      goto errimp2;
    }

    /* store it */
    if(passwd != NULL) {
      sk = pcpkey_encrypt(ptx, sk, passwd);
    }
    else {
      char *passphrase;
      pcp_readpass(ptx, &passphrase,
                   "Enter passphrase for key encryption",
                   "Enter the passphrase again", 1, NULL);
    
      if(strnlen(passphrase, 1024) > 0) {
        /* encrypt the key */
        sk = pcpkey_encrypt(ptx, sk, passphrase);
        sfree(passphrase);
      }
      else {
        /* ask for confirmation if we shall store it in the clear */
        char *yes = pcp_getstdin(
                                 "WARNING: secret key will be stored unencrypted. Are you sure [yes|NO]?");
        if(strncmp(yes, "yes", 1024) != 0) {
          free(yes);
          goto errimp1;
        }
        free(yes);
      }
    }

    if(sk != NULL) {
      /* store it to the vault if we got it til here */
      if(pcp_sanitycheck_key(ptx, sk) == 0) {
        if(pcp_storekey(sk) == 0) {
          pcpkey_printshortinfo(sk); 
          success = 0;
        }
      }
    }
  }


errimp2:
  if(keysig != NULL) {
    ucfree(keysig->blob, keysig->size);
    ucfree(keysig, sizeof(pcp_keysig_t));
  }
  
  if(bundle != NULL) {
    free(bundle);
  }
  
  if(pub != NULL) {
    ucfree(pub, sizeof(pcp_pubkey_t));
  }
  
  if(sk != NULL) {
    ucfree(sk, sizeof(pcp_key_t));
  }

  ucfree(buf, bufsize);

 errimp1:
  ps_close(pin);

  return success;
}
