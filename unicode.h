/* Rune are simple integers */
typedef unsigned int Rune;

#define NORUNE          0x80000000u
#define WRONGRUNE       0xfffdu

#define risascii(r) ((r) < 0x7f)

int utf8_decode_len(unsigned char);
int utf8_rune_len(Rune);
int utf8_rune_nlen(const Rune *, int);
int utf8_encode_rune(Rune, unsigned char *, int);
int utf8_decode_rune(Rune *, const unsigned char *, int);
int unicode_rune_width(Rune);
