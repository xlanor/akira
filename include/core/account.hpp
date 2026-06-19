#ifndef AKIRA_ACCOUNT_HPP
#define AKIRA_ACCOUNT_HPP

#include <cstdint>
#include <string>

struct Account {
    std::string onlineId;
    std::string accountId;
    std::string refreshToken;
    std::string accessToken;
    int64_t tokenExpiresAt = 0;
    std::string duid;

    bool isRemote() const { return !refreshToken.empty(); }
    bool empty() const { return onlineId.empty() && accountId.empty(); }

    std::string label() const {
        if (!onlineId.empty()) return onlineId;
        if (!accountId.empty()) return accountId;
        return "";
    }
};

#endif // AKIRA_ACCOUNT_HPP
