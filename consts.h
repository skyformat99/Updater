
#ifndef TOX_CONSTS
#define TOX_CONSTS

#include <stdint.h>
#include <sodium.h>

#define APPENDED_VERSION_LENGTH (6 + 2)

#define VERSION 3

#define NUMBER_UPDATE_HOSTS 2

static const char *TOX_DOWNNLOAD_HOSTS[NUMBER_UPDATE_HOSTS] = {
    "dl.utox.org",
    "dl.u.tox.im"
};

#define SELF_UPDATER_FILE_NAME "winselfpdate"
#define VERSION_FILE_NAME "version1"
static char GET_NAME[] = "win32-latest";

static const uint8_t TOX_SELF_PUBLICK_KEY[crypto_sign_ed25519_PUBLICKEYBYTES] = {
    0x88, 0x90, 0x5F, 0x29, 0x46, 0xBE, 0x7C, 0x4B, 0xBD, 0xEC, 0xE4, 0x67, 0x14, 0x9C, 0x1D, 0x78,
    0x48, 0xF4, 0xBC, 0x4F, 0xEC, 0x1A, 0xD1, 0xAD, 0x6F, 0x97, 0x78, 0x6E, 0xFE, 0xF3, 0xCD, 0xA1
};

static const uint8_t TOX_SELF_PUBLICK_UPDATE_KEY[crypto_sign_ed25519_PUBLICKEYBYTES] = {
    0x52, 0xA7, 0x9B, 0xCA, 0x48, 0x35, 0xD6, 0x34, 0x5E, 0x7D, 0xEF, 0x8B, 0x97, 0xC3, 0x54, 0x2D,
    0x37, 0x9A, 0x9A, 0x8B, 0x00, 0xEB, 0xF3, 0xA8, 0xAD, 0x03, 0x92, 0x3E, 0x0E, 0x50, 0x77, 0x58
};


#endif