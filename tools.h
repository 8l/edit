#ifndef TOOLS_H
#define TOOLS_H

/* unused - Use this macro to suppress
 * compiler warnings about a unused
 * function parameter/variable.
 */
static inline void _unused(void *x) { (void) x; }
#define unused(x) _unused(&(x))

/* static_assert - Static assertions.
 */
#define static_assert _Static_assert

#endif /* ndef TOOLS_H */
