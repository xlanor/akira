#ifndef AKIRA_SEMVER_HPP
#define AKIRA_SEMVER_HPP

#include <cctype>
#include <string>
#include <vector>

namespace akira::semver {

struct Parsed {
    long long major = 0;
    long long minor = 0;
    long long patch = 0;
    std::vector<std::string> pre;
    bool valid = false;
};

inline Parsed parse(const std::string& in) {
    Parsed v;
    std::string s = in;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
        s = s.substr(1);

    auto plus = s.find('+');
    if (plus != std::string::npos)
        s = s.substr(0, plus);

    std::string core = s;
    std::string pre;
    bool hasPre = false;
    auto dash = s.find('-');
    if (dash != std::string::npos) {
        core = s.substr(0, dash);
        pre = s.substr(dash + 1);
        hasPre = true;
    }

    long long* out[3] = { &v.major, &v.minor, &v.patch };
    size_t start = 0;
    for (int i = 0; i < 3; i++) {
        size_t dot = core.find('.', start);
        std::string part = core.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (part.empty())
            return v;
        for (char c : part)
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return v;
        try {
            *out[i] = std::stoll(part);
        } catch (...) {
            return v;
        }
        if (dot == std::string::npos) {
            if (i < 2)
                return v;
            break;
        }
        start = dot + 1;
    }

    if (hasPre && pre.empty())
        return v;

    if (!pre.empty()) {
        size_t p = 0;
        while (true) {
            size_t d = pre.find('.', p);
            std::string id = pre.substr(p, d == std::string::npos ? std::string::npos : d - p);
            if (id.empty())
                return v;
            v.pre.push_back(id);
            if (d == std::string::npos)
                break;
            p = d + 1;
        }
    }

    v.valid = true;
    return v;
}

inline bool isValid(const std::string& s) {
    return parse(s).valid;
}

namespace detail {

inline bool isNumericId(const std::string& s) {
    if (s.empty())
        return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

inline int compareIds(const std::string& a, const std::string& b) {
    bool na = isNumericId(a);
    bool nb = isNumericId(b);
    if (na && nb) {
        long long la = 0;
        long long lb = 0;
        try {
            la = std::stoll(a);
            lb = std::stoll(b);
        } catch (...) {
            return a < b ? -1 : (a > b ? 1 : 0);
        }
        if (la != lb)
            return la < lb ? -1 : 1;
        return 0;
    }
    if (na && !nb)
        return -1;
    if (!na && nb)
        return 1;
    if (a != b)
        return a < b ? -1 : 1;
    return 0;
}

}

inline int compare(const std::string& aStr, const std::string& bStr) {
    Parsed a = parse(aStr);
    Parsed b = parse(bStr);

    if (!a.valid && !b.valid)
        return 0;
    if (!a.valid)
        return -1;
    if (!b.valid)
        return 1;

    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;

    bool ap = !a.pre.empty();
    bool bp = !b.pre.empty();
    if (!ap && !bp)
        return 0;
    if (!ap && bp)
        return 1;
    if (ap && !bp)
        return -1;

    size_t n = a.pre.size() < b.pre.size() ? a.pre.size() : b.pre.size();
    for (size_t i = 0; i < n; i++) {
        int c = detail::compareIds(a.pre[i], b.pre[i]);
        if (c != 0)
            return c;
    }
    if (a.pre.size() != b.pre.size())
        return a.pre.size() < b.pre.size() ? -1 : 1;
    return 0;
}

}

#endif
