#ifndef AKIRA_EXCEPTION_HPP
#define AKIRA_EXCEPTION_HPP

#include <exception>
#include <string>

class Exception : public std::exception
{
private:
    const char *msg;

public:
    explicit Exception(const char *msg) : msg(msg) {}
    const char *what() const noexcept override { return msg; }
};

#endif // AKIRA_EXCEPTION_HPP
