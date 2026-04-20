#ifndef BGEN11_LOGGING_HPP
#define BGEN11_LOGGING_HPP

// Minimal stubs for conduit logging macros. bgen-generated code references
// these macros; they are replaced by no-ops at translation time. Define
// safe fallbacks in case any leak through.

#define LOG_INFOF(...)  (void)0
#define LOG_WARNF(...)  (void)0
#define LOG_ERRORF(...) (void)0
#define LOG_DEBUGF(...) (void)0
#define LOG_TRACEF(...) (void)0
#define LOG_INFO(...)   (void)0
#define LOG_WARN(...)   (void)0
#define LOG_ERROR(...)  (void)0
#define LOG_DEBUG(...)  (void)0
#define LOG_TRACE(...)  (void)0

#endif
