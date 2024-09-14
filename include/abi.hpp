#ifndef ABI_HPP
#define ABI_HPP

#if defined(__x86_64__) || defined(__amd64__)
#if defined(__unix__) || defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
#define AIO_SYSTEM_V_AMD64_ABI
#endif
#endif

#endif //ABI_HPP
