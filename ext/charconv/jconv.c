/*
 * jconv.c - alternative japanese code conversion routines
 *
 *  Copyright(C) 2002 by Shiro Kawai (shiro@acm.org)
 *
 *  Permission to use, copy, modify, distribute this software and
 *  accompanying documentation for any purpose is hereby granted,
 *  provided that existing copyright notices are retained in all
 *  copies and that this notice is included verbatim in all
 *  distributions.
 *  This software is provided as is, without express or implied
 *  warranty.  In no circumstances the author(s) shall be liable
 *  for any damages arising out of the use of this software.
 *
 *  $Id: jconv.c,v 1.3 2002-06-05 10:40:41 shirok Exp $
 */

/* Some iconv() implementations don't support japanese character encodings,
 * or have problems handling them.  This code provides an alternative way
 * to convert these encodings.
 */

/* This file handles conversion among UTF8, Shift-JIS, EUC_JP, and ISO2022JP.
 * Shift-JIS and EUC_JP are based on JIS X 0213:2000.  ISO2022JP partially
 * handles ISO2022-JP-3 as well.
 *
 * EUC_JP is used as a 'pivot' encoding, for it can naturally handle
 * JISX 0201, JISX 0208, JISX 0212 and JISx 0213 characters.
 */

#include "charconv.h"

#define ILLEGAL_SEQUENCE  -1
#define INPUT_NOT_ENOUGH  -2
#define OUTPUT_NOT_ENOUGH -3

#define INCHK(n)   do{if (inroom < (n)) return INPUT_NOT_ENOUGH;}while(0)
#define OUTCHK(n)  do{if (outroom < (n)) return OUTPUT_NOT_ENOUGH;}while(0)

/* Substitution characters.
 *  Unrecognized 1-byte character is substituted by SUBST1_CHAR.
 *  It's common to all encodings.
 *  Unrecognized or uncovertable multibyte character is substituted
 *  by so-called 'Geta-sign'.
 */
#define SUBST1_CHAR   '?'
#define EUCJ_SUBST2_CHAR1  0xa2
#define EUCJ_SUBST2_CHAR2  0xae
#define JIS_SUBST2_CHAR1   0x02
#define JIS_SUBST2_CHAR2   0x0e
#define SJIS_SUBST2_CHAR1  0x81
#define SJIS_SUBST2_CHAR2  0xac
#define UTF8_SUBST2_CHAR1   0xe3
#define UTF8_SUBST2_CHAR2   0x80
#define UTF8_SUBST2_CHAR3   0x93

#define EUCJ_SUBST                              \
  do { OUTCHK(2);                               \
       outptr[0] = EUCJ_SUBST2_CHAR1;           \
       outptr[1] = EUCJ_SUBST2_CHAR2;           \
       *outchars = 2; } while (0)

#define SJIS_SUBST                              \
  do { OUTCHK(2);                               \
       outptr[0] = SJIS_SUBST2_CHAR1;           \
       outptr[1] = SJIS_SUBST2_CHAR2;           \
       *outchars = 2; } while (0)

#define UTF8_SUBST                              \
  do { OUTCHK(3);                               \
       outptr[0] = UTF8_SUBST2_CHAR1;           \
       outptr[1] = UTF8_SUBST2_CHAR2;           \
       outptr[2] = UTF8_SUBST2_CHAR2;           \
       *outchars = 3; } while (0)

/*=================================================================
 * Shift JIS
 */

/* Shift_JISX0213 -> EUC-JP
 * 
 * Mapping anormalities
 *
 *   0x5c, 0x7e : Shift_JISX0213 mapping table maps 0x5c to U+00A5
 *       (YEN SIGN) and 0x7e to U+203E (OVERLINE).  But mapping so
 *       breaks the program code written in Shift JIS.   I map them
 *       to the corresponding ASCII chars.
 *   0xfd, 0xfe, 0xff : These are reserved bytes.  Apple uses these
 *       bytes for vendor extension:
 *        0xfd - U+00A9 COPYRIGHT SIGN     |EUC A9A6  |JISX0213
 *        0xfe - U+2122 TRADE MARK SIGN    |EUC 8FA2EF|JISX0212
 *        0xff - U+2026 HORIZONTAL ELLIPSIS|EUC A1C4  |JISX0208
 *       This is a one-direction mapping.
 *   0x80, 0xa0 : These are reserved bytes.  Replaced to the
 *       one-byte substitution character of destination encoding.
 *
 * Conversion scheme
 *   0x00-0x7f : corresponding ASCII range.
 *   0x80      : substitution character
 *   0x81 -- 0x9f : first byte (s1) of double byte range for JIS X 0213 m=1
 *   0xa0      : substitution character
 *   0xa1 -- 0xdf : JISX 0201 kana = s1-0x80
 *   0xe0 -- 0xef : first byte (s1) of double byte range for JIS X 0213 m=1
 *   0xf0 -- 0xfc : first byte (s1) of double byte range for JIS X 0213 m=2
 *   0xfd : U+00A9, EUC A9A6, JISX0213 (1, 0x09, 0x06)
 *   0xfe : U+2122, EUC 8FA2EF, JISX0212
 *   0xff : U+2026, EUC A1C4, JISX0208 (1, 0x01, 0x24)
 *
 *   For double-byte character, second byte s2 must be in the range of
 *   0x40 <= s2 <= 0x7e or 0x80 <= s2 <= 0xfc.  Otherwise, double-byte
 *   substitution character is used.
 *
 *     two bytes (s1, s2) maps to JIS X 0213 (m, k, t) by
 *        m = 1 if s1 <= 0xef, 2 otherwise
 *        k = (s1-0x80)*2 - ((s2 >= 0x80)? 1 : 0)  if s1 <= 0xef
 *            (s1-0x9e)*2 - ((s2 >= 0x80)? 1 : 0)  if s1 >= 0xf5
 *            otherwise, use the following table
 *               s1   k (s2>=0x80, s2<0x80)
 *              0xf0   (0x01, 0x08)
 *              0xf1   (0x03, 0x04)
 *              0xf2   (0x05, 0x0c)
 *              0xf3   (0x0e, 0x0d)
 *              0xf4   (0x0f, 0x4e)
 *        t = s2-0x3f if s2 < 0x7f
 *            s2-0x40 if s2 < 0x9f
 *            s2-0x9e otherwise
 *
 *     JIS X 0213 to EUC-JP is a straightfoward conversion.
 */

static int sjis2eucj(ScmConvInfo *cinfo, const char *inptr, int inroom,
                     char *outptr, int outroom, int *outchars)
{
    unsigned char s1, s2;
    static unsigned char cvt[] = { 0xa1, 0xa8, 0xa3, 0xa4, 0xa5, 0xac, 0xae, 0xad, 0xaf, 0xee };

    s1 = *(unsigned char *)inptr;
    if (s1 < 0x7f) {
        *outptr = s1;
        *outchars = 1;
        return 1;
    }
    if ((s1 > 0x80 && s1 < 0xa0) || (s1 >= 0xe0 && s1 <= 0xfc)) {
        /* Double byte char */
        unsigned char m, e1, e2;
        INCHK(2);
        s2 = *(unsigned char*)(inptr+1);
        if (s2 < 0x40 || s2 > 0xfc) {
            EUCJ_SUBST;
            return 2;
        }
        
        if (s1 <= 0xef) {
            OUTCHK(2);
            m = 1;
            e1 = (s1-0x80)*2 + 0xa0 - ((s2 >= 0x80)? 1 : 0);
        } else if (s1 >= 0xf5) {
            OUTCHK(3);
            m = 2;
            e1 = (s1-0x9e)*2 + 0xa0 - ((s2 >= 0x80)? 1 : 0);
        } else {
            OUTCHK(3);
            m = 2;
            e1 = cvt[(s1-0xf0)*2+((s2 < 0x80)? 1 : 0)];
        }
        
        if (s2 < 0x7f) {
            e2 = s1 - 0x3f + 0xa0;
        } else if (s2 < 0x9f) {
            e2 = s1 - 0x40 + 0xa0;
        } else {
            e2 = s1 - 0x9e + 0xa0;
        }
        if (m == 1) {
            outptr[0] = e1;
            outptr[1] = e2;
            *outchars = 2;
        } else {
            outptr[0] = 0x8f;
            outptr[1] = e1;
            outptr[2] = e2;
            *outchars = 3;
        }
        return 2;
    }
    if (s1 >= 0xa1 && s1 <= 0xdf) {
        /* JISX0201 KANA */
        OUTCHK(2);
        outptr[0] = 0x8e;
        outptr[1] = s1;
        *outchars = 2;
        return 1;
    }
    if (s1 == 0xfd) {
        /* copyright mark */
        OUTCHK(2);
        outptr[0] = 0xa9;
        outptr[1] = 0xa6;
        *outchars = 2;
        return 1;
    }
    if (s1 == 0xfe) {
        /* trademark sign.  this is not in JISX0213, but in JISX0212. */
        OUTCHK(3);
        outptr[0] = 0x8f;
        outptr[1] = 0xa2;
        outptr[2] = 0xef;
        *outchars = 3;
        return 1;
    }
    if (s1 == 0xff) {
        /* horizontal ellipsis. */
        OUTCHK(2);
        outptr[0] = 0xa1;
        outptr[1] = 0xc4;
        *outchars = 2;
        return 1;
    }
    
    /* s1 == 0x80 or 0xa0 */
    outptr[0] = SUBST1_CHAR;
    *outchars = 1;
    return 1;
}

/* EUC_JISX0213 -> Shift_JIS
 * 
 * Mapping anormalities
 *
 *   0x80--0xa0 except 0x8e and 0x8f : C1 region.
 *          Doesn't have corresponding SJIS bytes,
 *          so mapped to substitution char.
 *   0xff : reserved byte.  mapped to substitution char.
 *
 * Conversion scheme
 *   0x00-0x7f : corresponding ASCII range.
 *   0x80--0x8d : substitution char.
 *   0x8e : leading byte of JISX 0201 kana
 *   0x8f : leading byte of JISX 0212 or JISX 0213 plane 2
 *   0x90--0xa0 : substitution char.
 *   0xa1--0xfe : first byte (e1) of JISX 0213 plane 1
 *   0xff : substitution char
 *
 *   For double or trible-byte character, subsequent byte has to be in
 *   the range between 0xa1 and 0xfe inclusive.  If not, it is replaced
 *   for the substitution character.
 *   
 *   If the first byte is in the range of 0xa1--0xfe, two bytes (e1, e2)
 *   is mapped to SJIS (s1, s2) by:
 *
 *     s1 = (e1 - 0xa0 + 0x101)/2 if 0xa1 <= e1 <= 0xde
 *          (e1 - 0xa0 + 0x181)/2 if 0xdf <= e1 <= 0xfe
 *     s2 = (e2 - 0xa0 + 0x3f) if even?(e1) && 0xa1 <= e2 <= 0xdf
 *          (e2 - 0xa0 + 0x40) if even?(e1) && 0xe0 <= e2 <= 0xfe
 *          (e2 - 0xa0 + 0x9e) if odd?(e1)
 *
 *   If the first byte is 0x8f, the second byte (e1) and the third byte
 *   (e2) is mapped to SJIS (s1, s2) by:
 *     if (0xee <= e1 <= 0xfe)  s1 = (e1 - 0xa0 + 0x19b)/2
 *     otherwise, follow the table:
 *       e1 == 0xa1 or 0xa8  => s1 = 0xf0
 *       e1 == 0xa3 or 0xa4  => s1 = 0xf1
 *       e1 == 0xa5 or 0xac  => s1 = 0xf2
 *       e1 == 0xae or 0xad  => s1 = 0xf3
 *       e1 == 0xaf          => s1 = 0xf4
 *     If e1 is other value, it is JISX0212; we use substitution char.
 *     s2 is mapped with the same rule above.
 */

static int eucj2sjis(ScmConvInfo *cinfo, const char *inptr, int inroom,
                     char *outptr, int outroom, int *outchars)
{
    unsigned char e1, e2;
    e1 = *(unsigned char*)inptr;
    if (e1 <= 0x7f) {
        *outptr = e1;
        *outchars = 1;
        return 1;
    }
    if (e1 >= 0xa1 && e1 <= 0xfe) {
        /* double byte char (JISX 0213 plane 1) */
        unsigned char s1, s2;
        INCHK(2);
        e2 = inptr[1];
        if (e2 < 0xa1 || e2 == 0xff) {
            SJIS_SUBST;
            return 2;
        }
        OUTCHK(2);
        if (e1 <= 0xde) s1 = (e1 - 0xa0 + 0x101)/2;
        else            s1 = (e1 - 0xa0 + 0x181)/2;
        if (e1%2) {
            s2 = e2 - 0xa0 + 0x9e;
        } else {
            if (e2 < 0xdf) s2 = e2 - 0xa0 + 0x3f;
            else           s2 = e2 - 0xa0 + 0x40;
        }
        outptr[0] = e1;
        outptr[1] = e2;
        *outchars = 2;
        return 2;
    }
    if (e1 == 0x8e) {
        /* JISX 0201 kana */
        INCHK(2);
        e2 = inptr[1];
        if (e2 < 0xa1 || e2 == 0xff) {
            *outptr = SUBST1_CHAR;
        } else {
            *outptr = e2;
        }
        *outchars = 1;
        return 2;
    }
    if (e1 == 0x8f) {
        /* triple byte char */
        unsigned char s1, s2;
        unsigned char cvt[] = { 0xf0, 0, 0xf1, 0xf1, 0xf2, 0, 0, 0xf0, 0, 0, 0, 0xf2, 0xf3, 0xf3, 0xf4 };
        
        INCHK(3);
        OUTCHK(2);
        e1 = inptr[1];
        e2 = inptr[2];
        if (e1 < 0xa1 || e1 == 0xff || e2 < 0xa1 || e2 == 0xff) {
            SJIS_SUBST;
            return 3;
        }
        if (e1 >= 0xee) {
            s1 = (e1 - 0xa0 + 0x19b)/2;
        } else if (e1 >= 0xb0) {
            SJIS_SUBST;
            return 3;
        } else {
            s1 = cvt[e1-0xa1];
            if (s1 == 0) {
                SJIS_SUBST;
                return 3;
            }
        }
        if (e1%2) {
            s2 = e2 - 0xa0 + 0x9e;
        } else {
            if (e2 < 0xdf) s2 = e2 - 0xa0 + 0x3f;
            else           s2 = e2 - 0xa0 + 0x40;
        }
        outptr[0] = e1;
        outptr[1] = e2;
        *outchars = 2;
        return 3;
    }
    /* no corresponding char */
    *outptr = SUBST1_CHAR;
    *outchars = 1;
    return 1;
}

/*=================================================================
 * UTF8
 */

/* Conversion between UTF8 and EUC_JP is based on the table found at
 * http://isweb11.infoseek.co.jp/computer/wakaba/table/jis-note.ja.html
 *
 * There are some characters in JISX0213 that can't be represented
 * in a single Unicode character, but can be with a combining character.
 * In such case, EUC_JP to UTF8 conversion uses combining character,
 * but UTF8 to EUC_JP conversion translates the combining character into
 * another character.  For example, a single JISX0213 katakana 'nga'
 * (hiragana "ka" with han-dakuon mark) will translates to Unicode
 * U+304B+309A (HIRAGANA LETTER KA + COMBINING KATAKANA-HIRAGANA SEMI-VOICED
 * SOUND MARK).  When this sequence is converted to EUC_JP again, it
 * becomes EUCJ 0xA4AB + 0xA1AC.  This is an implementation limitation,
 * and should be removed in later release.
 */

/* [UTF8 -> EUC_JP conversion]
 *
 * EUC-JP has the corresponding characters to the wide range of
 * UCS characters.
 *
 *   UCS4 character   # of EUC_JP characters
 *   ---------------------------------------
 *     U+0000+0xxx    564
 *     U+0000+1xxx      6
 *     U+0000+2xxx    321
 *     U+0000+3xxx    422
 *     U+0000+4xxx    347
 *     U+0000+5xxx   1951
 *     U+0000+6xxx   2047
 *     U+0000+7xxx   1868
 *     U+0000+8xxx   1769
 *     U+0000+9xxx   1583
 *     U+0000+fxxx    241
 *     U+0002+xxxx    302
 *
 * It is so wide and so sparse that naive lookup table implementation from
 * UCS to EUC can be space-wasting.  I use hierarchical table with some
 * ad-hoc heuristics.   Since the hierarchical table is used, I directly
 * translates UTF8 to EUC_JP, without converting it to UCS4.
 *
 * Strategy outline: say input consists of bytes named u0, u1, ....
 *
 *  u0 <= 0x7f  : ASCII range
 *  u0 in [0xc2-0xd1] : UTF8 uses 2 bytes.  Some mappings within this range
 *         is either very regular or very small, and they are
 *         hardcoded.   Other mappings uses table lookup.
 *  u0 == 0xe1  : UTF8 uses 3 bytes.  There are only 6 characters in this
 *         range, and it is hardcoded.
 *  u0 in [0xe2-0xe9, 0xef] : Large number of characters are in this range.
 *         Two-level table of 64 entries each is used to dispatch the
 *         characters.
 *  u0 == 0xf0  : UTF8 uses 4 bytes.  u1 is in [0xa0-0xaa].  u2 and u3 is
 *         used for dispatch table of 64 entries each.
 *
 * The final table entry is unsigned short.  0x0000 means no corresponding
 * character is defined in EUC_JP.  >=0x8000 is the EUC_JP character itself.
 * < 0x8000 means the character is in G3 plane; 0x8f should be preceded,
 * and 0x8000 must be added to the value.
 */

#include "ucs2eucj.c"

/* Emit given euc char */
static inline int utf2euc_emit_euc(unsigned short euc, int inchars, char *outptr, int outroom, int *outchars)
{
    if (euc == 0) {
        EUCJ_SUBST;
    } else if (euc < 0x8000) {
        OUTCHK(3);
        outptr[0] = 0x8f;
        outptr[1] = (euc >> 8) + 0x80;
        outptr[2] = euc & 0xff;
        *outchars = 3;
    } else {
        OUTCHK(2);
        outptr[0] = (euc >> 8) + 0x80;
        outptr[1] = euc & 0xff;
        *outchars = 2;
    }
    return inchars;
}

/* handle 2-byte UTF8 sequence.  0xc0 <= u0 <= 0xdf */
static inline int utf2euc_2(ScmConvInfo *cinfo, unsigned char u0,
                            const char *inptr, int inroom,
                            char *outptr, int outroom, int *outchars)
{
    unsigned char u1;
    unsigned short *etab = NULL;
    
    INCHK(2);
    u1 = (unsigned char)inptr[1];
    if (u1 < 0x80 || u1 >= 0xbf) return ILLEGAL_SEQUENCE;

    switch (u0) {
    case 0xc2: etab = utf2euc_c2; break;
    case 0xc3: etab = utf2euc_c3; break;
    case 0xc4: etab = utf2euc_c4; break;
    case 0xc5: etab = utf2euc_c5; break;
    case 0xc6:
        if (u1 == 0x93) { /* U+0193 -> euc ABA9 */
            return utf2euc_emit_euc(0xaba9, 2, outptr, outroom, outchars);
        } else break;
    case 0xc7: etab = utf2euc_c7; break;
    case 0xc9: etab = utf2euc_c9; break;
    case 0xca: etab = utf2euc_ca; break;
    case 0xcb: etab = utf2euc_cb; break;
    case 0xcc: etab = utf2euc_cc; break;
    case 0xcd:
        if (u1 == 0xa1) { /* U+0361 -> euc ABD2 */
            return utf2euc_emit_euc(0xabd2, 2, outptr, outroom, outchars);
        } else break;
    case 0xce: etab = utf2euc_ce; break;
    case 0xcf: etab = utf2euc_cf; break;
    default:
        break;
    }
    if (etab != NULL) {
        /* table lookup */
        return utf2euc_emit_euc(etab[u1-0x80], 2, outptr, outroom, outchars);
    }
    EUCJ_SUBST;
    return 2;
}

/* handle 3-byte UTF8 sequence.  0xe0 <= u0 <= 0xef */
static inline int utf2euc_3(ScmConvInfo *cinfo, unsigned char u0,
                            const char *inptr, int inroom,
                            char *outptr, int outroom, int *outchars)
{
    unsigned char u1, u2;
    unsigned char *tab1 = NULL;
    unsigned short (*tab2)[64] = NULL;

    INCHK(3);
    u1 = (unsigned char)inptr[1];
    u2 = (unsigned char)inptr[2];
    
    switch (u0) {
    case 0xe1: /* special case : there's only 6 chars */
        {
            unsigned short euc = 0;
            if (u1 == 0xb8) {
                if (u2 == 0xbe)      euc = 0xa8f2;
                else if (u2 == 0xbf) euc = 0xa8f3;
            } else if (u1 == 0xbd) {
                if (u2 == 0xb0)      euc = 0xabc6;
                else if (u2 == 0xb1) euc = 0xabc7;
                else if (u2 == 0xb2) euc = 0xabd0;
                else if (u2 == 0xb3) euc = 0xabd1;
            }
            return utf2euc_emit_euc(euc, 3, outptr, outroom, outchars);
        }
    case 0xe2: tab1 = utf2euc_e2; tab2 = utf2euc_e2_xx; break;
    case 0xe3: tab1 = utf2euc_e3; tab2 = utf2euc_e3_xx; break;
    case 0xe4: tab1 = utf2euc_e4; tab2 = utf2euc_e4_xx; break;
    case 0xe5: tab1 = utf2euc_e5; tab2 = utf2euc_e5_xx; break;
    case 0xe6: tab1 = utf2euc_e6; tab2 = utf2euc_e6_xx; break;
    case 0xe7: tab1 = utf2euc_e7; tab2 = utf2euc_e7_xx; break;
    case 0xe8: tab1 = utf2euc_e8; tab2 = utf2euc_e8_xx; break;
    case 0xe9: tab1 = utf2euc_e9; tab2 = utf2euc_e9_xx; break;
    case 0xef: tab1 = utf2euc_ef; tab2 = utf2euc_ef_xx; break;
    default:
        break;
    }
    if (tab1 != NULL) {
        unsigned char ind = tab1[u1-0x80];
        if (ind != 0) {
            return utf2euc_emit_euc(tab2[ind-1][u2-0x80], 3, outptr, outroom, outchars);
        }
    }
    EUCJ_SUBST;
    return 3;
}

/* handle 4-byte UTF8 sequence.  u0 == 0xf0, 0xa0 <= u1 <= 0xaa */
static inline int utf2euc_4(ScmConvInfo *cinfo, unsigned char u0,
                            const char *inptr, int inroom,
                            char *outptr, int outroom, int *outchars)
{
    unsigned char u1, u2, u3;
    unsigned short *tab = NULL;

    INCHK(4);
    if (u0 != 0xf0) {
        EUCJ_SUBST;
        return 4;
    }
    u1 = (unsigned char)inptr[1];
    u2 = (unsigned char)inptr[2];
    u3 = (unsigned char)inptr[3];
    
    switch (u1) {
    case 0xa0: tab = utf2euc_f0_a0; break;
    case 0xa1: tab = utf2euc_f0_a1; break;
    case 0xa2: tab = utf2euc_f0_a2; break;
    case 0xa3: tab = utf2euc_f0_a3; break;
    case 0xa4: tab = utf2euc_f0_a4; break;
    case 0xa5: tab = utf2euc_f0_a5; break;
    case 0xa6: tab = utf2euc_f0_a6; break;
    case 0xa7: tab = utf2euc_f0_a7; break;
    case 0xa8: tab = utf2euc_f0_a8; break;
    case 0xa9: tab = utf2euc_f0_a9; break;
    case 0xaa: tab = utf2euc_f0_aa; break;
    default:
        break;
    }
    if (tab != NULL) {
        int i;
        unsigned short u2u3 = u2*256 + u3;
        for (i=0; tab[i]; i+=2) {
            if (tab[i] == u2u3) {
                return utf2euc_emit_euc(tab[i+1], 4, outptr, outroom, outchars);
            }
        }
    }
    EUCJ_SUBST;
    return 4;
}

/* Body of UTF8 -> EUC_JP conversion */
static int utf2eucj(ScmConvInfo *cinfo, const char *inptr, int inroom,
                    char *outptr, int outroom, int *outchars)
{
    unsigned char u0;
    
    u0 = (unsigned char)inptr[0];
    if (u0 <= 0x7f) {
        *outptr = u0;
        *outchars = 1;
        return 1;
    }
    if (u0 <= 0xbf) {
        /* invalid UTF8 sequence */
        return ILLEGAL_SEQUENCE;
    }
    if (u0 <= 0xdf) {
        /* 2-byte UTF8 sequence */
        return utf2euc_2(cinfo, u0, inptr, inroom, outptr, outroom, outchars);
    }
    if (u0 <= 0xef) {
        /* 3-byte UTF8 sequence */
        return utf2euc_3(cinfo, u0, inptr, inroom, outptr, outroom, outchars);
    }
    if (u0 <= 0xf7) {
        /* 4-byte UTF8 sequence */
        return utf2euc_4(cinfo, u0, inptr, inroom, outptr, outroom, outchars);
    }
    if (u0 <= 0xfb) {
        /* 5-byte UTF8 sequence */
        INCHK(5);
        EUCJ_SUBST;
        return 5;
    }
    if (u0 <= 0xfd) {
        /* 6-byte UTF8 sequence */
        INCHK(6);
        EUCJ_SUBST;
        return 6;
    }
    return ILLEGAL_SEQUENCE;
}

/* [EUC_JP -> UTF8 conversion]
 *
 * Conversion strategy:
 *   If euc0 is in ASCII range, or C1 range except 0x8e or 0x8f, map it as is.
 *   If euc0 is 0x8e, use JISX0201-KANA table.
 *   If euc0 is 0x8f, use JISX0213 plane 2 table.
 *   If euc0 is in [0xa1-0xfe], use JISX0213 plane1 table.
 *   If euc0 is 0xa0 or 0xff, return ILLEGAL_SEQUENCE.
 *
 * JISX0213 plane2 table is consisted by a 2-level tree.  The first-level
 * returns an index to the second-level table by (euc1 - 0xa1).  Only the
 * range of JISX0213 defined region is converted; JISX0212 region will be
 * mapped to the substitution char.
 */

#include "eucj2ucs.c"

/* UTF8 utility.  Similar stuff is included in gauche/char_utf_8.h
   if the native encoding is UTF8, but not otherwise.
   So I include them here as well. */

/* Given UCS char, return # of bytes required for UTF8 encoding. */
#define UCS2UTF_NBYTES(ucs)                      \
    (((ucs) < 0x80) ? 1 :                        \
     (((ucs) < 0x800) ? 2 :                      \
      (((ucs) < 0x10000) ? 3 :                   \
       (((ucs) < 0x200000) ? 4 :                 \
        (((ucs) < 0x4000000) ? 5 : 6)))))

static int ucs4_to_utf8(unsigned int ucs, char *cp)
{
    if (ucs < 0x80) {
        *cp = ucs;
    }
    else if (ucs < 0x800) {
        *cp++ = ((ucs>>6)&0x1f) | 0xc0;
        *cp = (ucs&0x3f) | 0x80;
    }
    else if (ucs < 0x10000) {
        *cp++ = ((ucs>>12)&0x0f) | 0xe0;
        *cp++ = ((ucs>>6)&0x3f) | 0x80;
        *cp = (ucs&0x3f) | 0x80;
    }
    else if (ucs < 0x200000) {
        *cp++ = ((ucs>>18)&0x07) | 0xf0;
        *cp++ = ((ucs>>12)&0x3f) | 0x80;
        *cp++ = ((ucs>>6)&0x3f) | 0x80;
        *cp = (ucs&0x3f) | 0x80;
    }
    else if (ucs < 0x4000000) {
        *cp++ = ((ucs>>24)&0x03) | 0xf8;
        *cp++ = ((ucs>>18)&0x3f) | 0x80;
        *cp++ = ((ucs>>12)&0x3f) | 0x80;
        *cp++ = ((ucs>>6)&0x3f) | 0x80;
        *cp = (ucs&0x3f) | 0x80;
    } else {
        *cp++ = ((ucs>>30)&0x1) | 0xfc;
        *cp++ = ((ucs>>24)&0x3f) | 0x80;
        *cp++ = ((ucs>>18)&0x3f) | 0x80;
        *cp++ = ((ucs>>12)&0x3f) | 0x80;
        *cp++ = ((ucs>>6)&0x3f) | 0x80;
        *cp++ = (ucs&0x3f) | 0x80;
    }
}

/* Given 'encoded' ucs, emit utf8.  'Encoded' ucs is the entry of the
   conversion table.  If ucs >= 0x100000, it is composed by two UCS2
   character.  Otherwise, it is one UCS4 character. */
static inline int eucj2utf_emit_utf(unsigned int ucs, int inchars,
                                    char *outptr, int outroom, int *outchars)
{
    if (ucs == 0) {
        UTF8_SUBST;
    } else if (ucs < 0x100000) {
        int outreq = UCS2UTF_NBYTES(ucs);
        OUTCHK(outreq);
        ucs4_to_utf8(ucs, outptr);
        *outchars = outreq;
    } else {
        /* we need two UCS characters */
        unsigned int ucs0 = (ucs >> 16) & 0xffff;
        unsigned int ucs1 = ucs & 0xfff;
        int outreq0 = UCS2UTF_NBYTES(ucs0);
        int outreq1 = UCS2UTF_NBYTES(ucs1);
        OUTCHK(outreq0+outreq1);
        ucs4_to_utf8(ucs0, outptr);
        ucs4_to_utf8(ucs1, outptr+outreq0);
        *outchars = outreq0+outreq1;
    }
    return inchars;
}

static int eucj2utf(ScmConvInfo *cinfo, const char *inptr, int inroom,
                    char *outptr, int outroom, int *outchars)
{
    unsigned char e0, e1, e2;
    unsigned int ucs;
    
    e0 = (unsigned char)inptr[0];
    if (e0 < 0xa0) {
        if (e0 == 0x8e) {
            /* JIS X 0201 KANA */
            INCHK(2);
            e1 = (unsigned char)inptr[1];
            if (e1 < 0xa1 || e1 > 0xdf) return ILLEGAL_SEQUENCE;
            ucs = 0xff61 + (e1 - 0xa1);
            return eucj2utf_emit_utf(ucs, 2, outptr, outroom, outchars);
        }
        else if (e0 == 0x8f) {
            /* JIS X 0213 plane 2 */
            int index;
            
            INCHK(3);
            e1 = (unsigned char)inptr[1];
            e2 = (unsigned char)inptr[2];
            if (e1 < 0xa1 || e1 > 0xfe || e2 < 0xa1 || e2 > 0xfe) {
                return ILLEGAL_SEQUENCE;
            }
            index = euc_jisx0213_2_index[e1 - 0xa1];
            if (index < 0) {
                UTF8_SUBST;
                return 3;
            }
            ucs = euc_jisx0213_2_to_ucs2[index][e2 - 0xa1];
            return eucj2utf_emit_utf(ucs, 3, outptr, outroom, outchars);
        }
    }
    if (e0 > 0xa0 && e0 < 0xff) {
        /* JIS X 0213 plane 1 */
        INCHK(2);
        e1 = (unsigned char)inptr[1];
        if (e1 < 0xa1 || e1 > 0xfe) return ILLEGAL_SEQUENCE;
        ucs = euc_jisx0213_1_to_ucs2[e0 - 0xa1][e1 - 0xa1];
        return eucj2utf_emit_utf(ucs, 2, outptr, outroom, outchars);
    }
    return ILLEGAL_SEQUENCE;
}

/*=================================================================
 * JCONV - the entry
 */

static struct conv_support_rec {
    const char *name;
    int code;
} conv_supports[] = {
    { "euc_jp",         JCONV_NONE },
    { "eucjp",          JCONV_NONE },
    { "eucj",           JCONV_NONE },
    { "euc_jisx0213",   JCONV_NONE },
    { "shift_jis",      JCONV_SJIS },
    { "shiftjis",       JCONV_SJIS },
    { "sjis",           JCONV_SJIS },
    { "utf-8",          JCONV_UTF8 },
    { "utf8",           JCONV_UTF8 },
    { NULL, 0 }
};

static int conv_name_match(const char *s, const char *t)
{
    const char *p, *q;
    for (p=s, q=t; *p && *q; p++, q++) {
        if (*p == '-' || *p == '_') {
            if (*q != '-' && *q == '-') return FALSE;
        } else {
            if (tolower(*p) != tolower(*q)) return FALSE;
        }
    }
    if (*p || *q) return FALSE;
    return TRUE;
}

static int conv_name_find(const char *name)
{
    struct conv_support_rec *cvtab = conv_supports;
    for (; cvtab->name; cvtab++) {
        if (conv_name_match(name, cvtab->name)) {
            return cvtab->code;
        }
    }
    return -1;
}

/* Returns ScmConvInfo, with filling inconv, outconv and handle field.
   Note that the other fields are not initialized.
   If no conversion is possible, returns NULL. */
ScmConvInfo *jconv_open(const char *toCode, const char *fromCode)
{
    ScmConvInfo *info;
    int inconv, outconv;
#if 0 /*for now*/
    inconv = conv_name_find(fromCode);
    outconv = conv_name_find(fromCode);
#else
    inconv = -1;
    outconv = -1;
#endif
    if (inconv < 0 && outconv < 0) {
#ifdef HAVE_ICONV_H        
        iconv_t handle = iconv_open(toCode, fromCode);
        if (handle == (iconv_t)-1) return NULL;
        info = SCM_NEW(ScmConvInfo);
        info->inconv = info->outconv = -1;
        info->handle = handle;
        info->toCode = toCode;
        info->fromCode = fromCode;
        return info;
#else /*!HAVE_ICONV_H*/
        return NULL;
#endif
    }
    info = SCM_NEW(ScmConvInfo);
    info->inconv = inconv;
    info->outconv = outconv;
    info->handle = (iconv_t)-1;
    info->toCode = toCode;
    info->fromCode = fromCode;
    return info;
}

int jconv_close(ScmConvInfo *info)
{
    int r = 0;
#ifdef HAVE_ICONV_H
    if (info->handle != (iconv_t)-1) {
        r = iconv_close(info->handle);
        info->handle = (iconv_t)-1;
    }
#endif /*HAVE_ICONV_H*/
    return r;
}

int jconv(ScmConvInfo *info,
                   const char **inptr, int *inroom,
                   char **outptr, int *outroom)
{
#ifdef HAVE_ICONV_H
    if (info->handle != (iconv_t)-1) {
        return iconv(info->handle, inptr, inroom, outptr, outroom);
    }
#endif /*HAVE_ICONV_H*/
    /*WRITEME*/
    return EINVAL;
}

