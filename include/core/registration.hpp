#ifndef AKIRA_REGISTRATION_HPP
#define AKIRA_REGISTRATION_HPP

#include <cstdint>

#include <chiaki/session.h>

struct Registration {
    int64_t consoleId = 0;
    int64_t profileId = 0;

    uint8_t serverMac[6] = {0};
    char rpRegistKey[CHIAKI_SESSION_AUTH_SIZE] = {0};
    uint32_t rpKeyType = 0;
    uint8_t rpKey[0x10] = {0};
};

#endif // AKIRA_REGISTRATION_HPP
